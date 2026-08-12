"""
XIAO ESP32S3 Sense 카메라 -> USB 시리얼 캡처(Base64) -> Claude Vision(VLM) 음식 인식 -> 로컬 웹 확인.

브라우저에 실시간 미리보기(MJPEG)가 항상 표시되고, "촬영" 버튼을 누르면
그 순간 고화질 프레임 1장을 따로 캡처해서 Claude에게 인식시킨다.

사용법:
  1) pip install -r requirements.txt
  2) D:/2026-Makerthon/kitchen-test/.env 에 API 키가 들어 있어야 함 (CLAUDE_API=... 또는 ANTHROPIC_API_KEY=...)
  3) python app.py
  4) 브라우저에서 http://127.0.0.1:5000 접속

보드가 시리얼로 이미지를 못 보내거나 사진이 옆으로 누워 보이면 환경변수로 조정:
  SERIAL_PORT=COM9 (기본값)
  ROTATE_DEGREES=0|90|180|270 (기본값 0)
  CLAUDE_MODEL=claude-haiku-4-5 (기본값, 빠른 테스트용)

보안: API 키는 .env에서만 읽고, 로그/응답/화면에 절대 출력하지 않는다.
"""

import base64
import io
import json
import os
import sys
import threading
import time

if sys.stdout.encoding and sys.stdout.encoding.lower() != "utf-8":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")

import anthropic
import requests
import serial
from dotenv import load_dotenv
from flask import Flask, Response, jsonify, render_template
from PIL import Image

# .env 로드 (이 파일 상위 폴더의 .env: D:/2026-Makerthon/kitchen-test/.env)
_ENV_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), ".env")
load_dotenv(_ENV_PATH)

SERIAL_PORT = os.environ.get("SERIAL_PORT", "COM9")
BAUD_RATE = int(os.environ.get("BAUD_RATE", "921600"))
ROTATE_DEGREES = int(os.environ.get("ROTATE_DEGREES", "0"))
MODEL = os.environ.get("CLAUDE_MODEL", "claude-haiku-4-5")
BACKEND_URL = os.environ.get("BACKEND_URL", "http://127.0.0.1:8000")

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
STATIC_DIR = os.path.join(BASE_DIR, "static")
LATEST_IMAGE_PATH = os.path.join(STATIC_DIR, "latest.jpg")
DETECT_IMAGE_PATH = os.path.join(STATIC_DIR, "detect_latest.jpg")
DATA_DIR = os.path.join(BASE_DIR, "data")
INVENTORY_PATH = os.path.join(DATA_DIR, "inventory.json")
INVENTORY_IMAGE_DIR = os.path.join(STATIC_DIR, "inventory")

FOOD_CATEGORIES = ["야채", "과일", "유제품", "육류", "해산물", "음료", "소스/조미료", "기타"]

ITEM_SCHEMA = {
    "type": "object",
    "properties": {
        "is_fridge_item": {
            "type": "boolean",
            "description": (
                "냉장고에 넣을 만한 물건인지 여부. 음식/식재료뿐 아니라 "
                "음료수, 물병, 캔, 반찬통 등 밀폐 용기도 모두 포함해서 판단"
            ),
        },
        "item_name_ko": {
            "type": "string",
            "description": "한국어 물건 이름 (예: 콜라, 우유, 딸기, 반찬통). 해당 없으면 빈 문자열",
        },
        "item_name_en": {
            "type": "string",
            "description": "영어 물건 이름. 해당 없으면 빈 문자열",
        },
        "food_category": {
            "type": "string",
            "enum": FOOD_CATEGORIES,
            "description": "식재료 종류 분류 (냉장고 재고 대시보드에서 그룹핑에 사용). 해당 없으면 '기타'",
        },
        "confidence": {"type": "string", "enum": ["low", "medium", "high"]},
        "description": {
            "type": "string",
            "description": "사진 속 물체에 대한 한두 문장 설명 (한국어)",
        },
    },
    "required": [
        "is_fridge_item",
        "item_name_ko",
        "item_name_en",
        "food_category",
        "confidence",
        "description",
    ],
    "additionalProperties": False,
}

PROMPT = (
    "이 사진은 냉장고 문이 열렸을 때 안으로 들어가는 물체를 촬영한 것입니다. "
    "사진 속 물체가 무엇인지 식별해 주세요. 음식이나 식재료뿐 아니라 "
    "음료수 캔/병, 생수, 우유팩, 밀폐 용기, 반찬통처럼 냉장고에 보관할 만한 "
    "모든 물건을 인식 대상으로 간주하세요. food_category는 다음 중에서 "
    f"가장 알맞은 것을 고르세요: {', '.join(FOOD_CATEGORIES)}. "
    "그런 물건이 전혀 아니라면(예: 벽, 조명, 손, 배경 등) is_fridge_item을 "
    "false로 표시하고 description에 무엇으로 보이는지 설명해 주세요."
)

app = Flask(__name__)

_serial_lock = threading.Lock()  # 시리얼 포트에 대한 모든 접근(미리보기/캡처 공용)을 직렬화
_ser: serial.Serial | None = None
_recv_buf = bytearray()  # 시리얼에서 아직 소비하지 않은 바이트를 보관하는 버퍼
_streaming_active = False  # ESP32가 현재 'S' 스트리밍 모드인지 여부

_preview_lock = threading.Lock()
_preview_frame: bytes | None = None
_capture_pause = threading.Event()  # set되어 있는 동안 미리보기 스레드는 대기

_anthropic_client: anthropic.Anthropic | None = None

_inventory_lock = threading.Lock()  # 재고 목록(_inventory) 및 그 JSON 파일에 대한 접근 보호
_inventory: list[dict] = []
_inventory_next_id = 1

_last_detection_lock = threading.Lock()  # 실시간 탐지 루프의 마지막 결과 (확인/추가 단계에서 사용)
_last_detection: dict = {"jpeg_bytes": None, "result": None}


def get_anthropic_client() -> anthropic.Anthropic:
    global _anthropic_client
    if _anthropic_client is None:
        api_key = os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("CLAUDE_API")
        if not api_key:
            raise RuntimeError(
                ".env에 ANTHROPIC_API_KEY 또는 CLAUDE_API 값이 설정되어 있지 않습니다."
            )
        _anthropic_client = anthropic.Anthropic(api_key=api_key)
    return _anthropic_client


def get_serial() -> serial.Serial:
    """시리얼 포트를 (필요할 때만) 열고 보드의 READY 신호를 기다린다.

    포트를 여는 순간 XIAO ESP32S3(USB CDC)가 리셋되도록 RTS만 펄스를 준다
    (esptool의 hard-reset과 동일한 시퀀스). DTR까지 같이 토글하면 보드가
    일반 부팅이 아니라 다운로드(부트로더) 모드로 들어가 버리므로 주의.
    매 요청마다 새로 열지 않고 커넥션을 재사용한다. 호출 전 _serial_lock을
    잡고 있어야 한다.
    """
    global _ser, _recv_buf, _streaming_active
    if _ser is not None and _ser.is_open:
        return _ser

    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    ser.dtr = False
    ser.rts = True
    time.sleep(0.15)
    ser.rts = False
    _recv_buf = bytearray()
    deadline = time.time() + 8.0
    while time.time() < deadline:
        line = ser.readline().decode(errors="ignore").strip()
        if line == "READY":
            break
        if line.startswith("ERR:"):
            ser.close()
            raise RuntimeError(f"ESP32 카메라 초기화 실패: {line}")
    else:
        ser.close()
        raise TimeoutError(
            f"{SERIAL_PORT}에서 READY 신호를 받지 못했습니다. 보드/포트를 확인하세요."
        )

    _ser = ser
    _streaming_active = False  # 새로 연결(=보드 리셋)되었으니 스트리밍 상태 초기화
    return _ser


def _read_until(ser: serial.Serial, marker: bytes, timeout: float) -> bytes:
    """marker가 나올 때까지 읽어서, marker 이전까지의 바이트를 반환한다.

    marker 뒤에 남는 바이트는 전역 버퍼(_recv_buf)에 보관되어 다음 호출에서 이어서 쓰인다.
    """
    global _recv_buf
    deadline = time.time() + timeout
    while True:
        idx = _recv_buf.find(marker)
        if idx != -1:
            result = bytes(_recv_buf[:idx])
            del _recv_buf[: idx + len(marker)]
            return result
        if time.time() > deadline:
            raise TimeoutError(f"'{marker!r}' 대기 중 타임아웃")
        chunk = ser.read(4096)
        if chunk:
            _recv_buf.extend(chunk)


def _read_one_frame(ser: serial.Serial, begin_timeout: float, data_timeout: float) -> bytes:
    """IMG_BEGIN/IMG_END 사이의 Base64 프레임 하나를 읽어 디코딩한다 (명령 전송은 하지 않음)."""
    deadline = time.time() + begin_timeout
    while True:
        remaining = deadline - time.time()
        if remaining <= 0:
            raise TimeoutError("IMG_BEGIN 대기 시간 초과")
        line = _read_until(ser, b"\n", timeout=remaining)
        text = line.decode(errors="ignore").strip()
        if text == "IMG_BEGIN":
            break
        if text.startswith("ERR:"):
            raise RuntimeError(f"ESP32 오류: {text}")

    b64_bytes = _read_until(ser, b"\nIMG_END\n", timeout=data_timeout)
    b64_text = b64_bytes.decode("ascii", errors="strict").strip()
    if not b64_text:
        raise ValueError("빈 이미지 데이터를 수신했습니다")

    return base64.b64decode(b64_text)


def capture_single_frame(ser: serial.Serial) -> bytes:
    """'C' 명령으로 고화질(SVGA) 프레임 1장을 캡처한다."""
    ser.write(b"C")
    return _read_one_frame(ser, begin_timeout=5.0, data_timeout=20.0)


def stop_streaming(ser: serial.Serial) -> None:
    """'X'로 스트리밍을 멈추고, 이미 전송 중이던 잔여 프레임을 모두 비운다."""
    ser.write(b"X")
    while True:
        try:
            _read_one_frame(ser, begin_timeout=0.3, data_timeout=3.0)
        except TimeoutError:
            break


def preview_worker() -> None:
    """백그라운드에서 계속 저해상도 프레임을 받아 최신 미리보기로 저장한다."""
    global _preview_frame, _streaming_active
    backoff = 0.5
    while True:
        if _capture_pause.is_set():
            time.sleep(0.05)
            continue
        try:
            with _serial_lock:
                ser = get_serial()
                if not _streaming_active:
                    ser.write(b"S")
                    _streaming_active = True
                jpeg = _read_one_frame(ser, begin_timeout=5.0, data_timeout=8.0)
            with _preview_lock:
                _preview_frame = jpeg
            backoff = 0.5
        except Exception:
            _streaming_active = False
            time.sleep(backoff)
            backoff = min(backoff * 2, 5.0)


def maybe_rotate(jpeg_bytes: bytes) -> bytes:
    if ROTATE_DEGREES % 360 == 0:
        return jpeg_bytes
    img = Image.open(io.BytesIO(jpeg_bytes))
    img = img.rotate(-ROTATE_DEGREES, expand=True)
    out = io.BytesIO()
    img.save(out, format="JPEG", quality=90)
    return out.getvalue()


def build_output_config() -> dict:
    config = {"format": {"type": "json_schema", "schema": ITEM_SCHEMA}}
    # effort 파라미터는 Haiku 계열 모델에서는 지원되지 않으므로 그 외 모델에만 지정
    if "haiku" not in MODEL:
        config["effort"] = "low"
    return config


def identify_item(jpeg_bytes: bytes) -> dict:
    b64 = base64.standard_b64encode(jpeg_bytes).decode("utf-8")
    client = get_anthropic_client()
    response = client.messages.create(
        model=MODEL,
        max_tokens=1024,
        output_config=build_output_config(),
        messages=[
            {
                "role": "user",
                "content": [
                    {
                        "type": "image",
                        "source": {
                            "type": "base64",
                            "media_type": "image/jpeg",
                            "data": b64,
                        },
                    },
                    {"type": "text", "text": PROMPT},
                ],
            }
        ],
    )
    text = next(b.text for b in response.content if b.type == "text")
    return json.loads(text)


def load_inventory() -> None:
    global _inventory, _inventory_next_id
    if os.path.exists(INVENTORY_PATH):
        with open(INVENTORY_PATH, encoding="utf-8") as f:
            _inventory = json.load(f)
        _inventory_next_id = max((item["id"] for item in _inventory), default=0) + 1


def save_inventory() -> None:
    os.makedirs(DATA_DIR, exist_ok=True)
    with open(INVENTORY_PATH, "w", encoding="utf-8") as f:
        json.dump(_inventory, f, ensure_ascii=False, indent=2)


def upsert_inventory(result: dict, jpeg_bytes: bytes) -> dict:
    """같은 이름의 물건이 이미 있으면 정보를 갱신하고 횟수를 올리며,
    없으면 새 항목으로 추가한다. 항상 최신 사진으로 썸네일을 교체한다."""
    global _inventory_next_id
    name_key = result["item_name_ko"].strip()
    now = time.time()

    with _inventory_lock:
        existing = next(
            (item for item in _inventory if item["item_name_ko"].strip() == name_key),
            None,
        )
        if existing:
            existing["item_name_en"] = result["item_name_en"]
            existing["food_category"] = result["food_category"]
            existing["confidence"] = result["confidence"]
            existing["description"] = result["description"]
            existing["count"] += 1
            existing["last_seen"] = now
            entry = existing
        else:
            entry = {
                "id": _inventory_next_id,
                "item_name_ko": result["item_name_ko"],
                "item_name_en": result["item_name_en"],
                "food_category": result["food_category"],
                "confidence": result["confidence"],
                "description": result["description"],
                "count": 1,
                "first_seen": now,
                "last_seen": now,
            }
            _inventory_next_id += 1
            _inventory.append(entry)

        os.makedirs(INVENTORY_IMAGE_DIR, exist_ok=True)
        image_path = os.path.join(INVENTORY_IMAGE_DIR, f"item_{entry['id']}.jpg")
        with open(image_path, "wb") as f:
            f.write(jpeg_bytes)

        save_inventory()
        return dict(entry)


def grouped_inventory() -> dict:
    """FOOD_CATEGORIES 순서대로, 항목이 있는 카테고리만 묶어서 반환한다."""
    with _inventory_lock:
        groups: dict[str, list[dict]] = {cat: [] for cat in FOOD_CATEGORIES}
        for item in sorted(_inventory, key=lambda x: x["item_name_ko"]):
            entry = dict(item)
            entry["image_url"] = (
                f"/static/inventory/item_{entry['id']}.jpg?t={int(entry['last_seen'])}"
            )
            groups.setdefault(entry["food_category"], []).append(entry)
        return {cat: groups[cat] for cat in FOOD_CATEGORIES if groups.get(cat)}


@app.route("/")
def index():
    return render_template(
        "index.html", serial_port=SERIAL_PORT, model=MODEL, backend_url=BACKEND_URL
    )


@app.route("/video_feed")
def video_feed():
    def generate():
        boundary = b"--frame"
        while True:
            with _preview_lock:
                frame = _preview_frame
            if frame:
                yield (
                    boundary + b"\r\n"
                    b"Content-Type: image/jpeg\r\n"
                    b"Content-Length: " + str(len(frame)).encode() + b"\r\n\r\n"
                    + frame + b"\r\n"
                )
            time.sleep(0.08)

    return Response(generate(), mimetype="multipart/x-mixed-replace; boundary=frame")


def capture_and_identify() -> tuple[bytes | None, dict | None, str | None]:
    """카메라로 고화질 프레임 1장을 캡처하고 Claude로 인식한다 (공용 헬퍼).

    반환값: (jpeg_bytes, result, error)
      - 캡처 자체가 실패하면 (None, None, error)
      - 캡처는 성공했지만 인식이 실패하면 (jpeg_bytes, None, error)
      - 둘 다 성공하면 (jpeg_bytes, result, None)
    """
    _capture_pause.set()
    try:
        global _streaming_active
        with _serial_lock:
            try:
                ser = get_serial()
                if _streaming_active:
                    stop_streaming(ser)
                    _streaming_active = False
                raw_jpeg = capture_single_frame(ser)
                # 캡처 후 실시간 미리보기 재개
                ser.write(b"S")
                _streaming_active = True
            except Exception as e:
                return None, None, str(e)
    finally:
        _capture_pause.clear()

    jpeg_bytes = maybe_rotate(raw_jpeg)

    try:
        result = identify_item(jpeg_bytes)
    except Exception as e:
        return jpeg_bytes, None, f"Claude API 호출 실패: {e}"

    return jpeg_bytes, result, None


def send_to_backend(result: dict, jpeg_bytes: bytes, device_id: str) -> dict:
    """인식된 물건 정보를 백엔드(FastAPI)의 /api/register/confirm 으로 보내서
    '내 냉장고 재고' DB에 등록한다 (Claude 재호출 없이 그대로 등록됨)."""
    payload = {
        "device_id": device_id,
        "class_name": result.get("food_category") or "기타",
        "label": result.get("item_name_ko") or result.get("item_name_en") or "알 수 없음",
        "confidence": result.get("confidence", "medium"),
        "image_base64": base64.standard_b64encode(jpeg_bytes).decode("utf-8"),
    }
    resp = requests.post(f"{BACKEND_URL}/api/register/confirm", json=payload, timeout=15)
    resp.raise_for_status()
    return resp.json()


@app.route("/api/capture", methods=["POST"])
def api_capture():
    jpeg_bytes, result, error = capture_and_identify()
    if jpeg_bytes is None:
        return jsonify({"ok": False, "stage": "capture", "error": error}), 502

    os.makedirs(STATIC_DIR, exist_ok=True)
    with open(LATEST_IMAGE_PATH, "wb") as f:
        f.write(jpeg_bytes)

    if result is None:
        return jsonify(
            {
                "ok": True,
                "image_url": f"/static/latest.jpg?t={int(time.time())}",
                "result": None,
                "inventory_entry": None,
                "error": error,
            }
        )

    inventory_entry = None
    backend_error = None
    if result.get("is_fridge_item"):
        inventory_entry = upsert_inventory(result, jpeg_bytes)
        try:
            send_to_backend(result, jpeg_bytes, device_id="flask-capture")
        except Exception as e:
            backend_error = f"백엔드 재고에는 반영되지 못했습니다: {e}"

    return jsonify(
        {
            "ok": True,
            "image_url": f"/static/latest.jpg?t={int(time.time())}",
            "result": result,
            "inventory_entry": inventory_entry,
            "error": None,
            "backend_error": backend_error,
        }
    )


@app.route("/api/detect", methods=["POST"])
def api_detect():
    """실시간 탐지 루프 한 사이클: 캡처 + 인식만 하고, 로컬 재고에는 자동으로
    추가하지 않는다 (사용자가 확인해야 /api/detect/confirm으로 백엔드에 등록됨)."""
    jpeg_bytes, result, error = capture_and_identify()
    if jpeg_bytes is None:
        return jsonify({"ok": False, "stage": "capture", "error": error}), 502

    os.makedirs(STATIC_DIR, exist_ok=True)
    with open(DETECT_IMAGE_PATH, "wb") as f:
        f.write(jpeg_bytes)

    with _last_detection_lock:
        _last_detection["jpeg_bytes"] = jpeg_bytes
        _last_detection["result"] = result

    return jsonify(
        {
            "ok": True,
            "image_url": f"/static/detect_latest.jpg?t={int(time.time())}",
            "result": result,
            "error": error,
        }
    )


@app.route("/api/detect/confirm", methods=["POST"])
def api_detect_confirm():
    """직전 /api/detect 결과를 사용자가 승인 -> 백엔드(FastAPI)에 등록 요청."""
    with _last_detection_lock:
        jpeg_bytes = _last_detection["jpeg_bytes"]
        result = _last_detection["result"]

    if jpeg_bytes is None or result is None:
        return jsonify({"ok": False, "error": "확인할 탐지 결과가 없습니다. 먼저 탐지를 실행하세요."}), 400

    try:
        backend_item = send_to_backend(result, jpeg_bytes, device_id="flask-live-detect")
    except Exception as e:
        return jsonify({"ok": False, "error": f"백엔드 전송 실패: {e}"}), 502

    return jsonify({"ok": True, "item": backend_item})


@app.route("/api/door")
def api_door():
    """백엔드의 문 개폐 상태를 그대로 프록시한다 (프론트 JS의 CORS 우회용)."""
    try:
        resp = requests.get(f"{BACKEND_URL}/api/door", timeout=5)
        resp.raise_for_status()
        return jsonify(resp.json())
    except Exception as e:
        return jsonify({"is_open": None, "device_id": None, "updated_at": None, "error": str(e)}), 502


@app.route("/api/inventory")
def api_inventory():
    return jsonify(grouped_inventory())


@app.route("/api/inventory/clear", methods=["POST"])
def api_inventory_clear():
    global _inventory
    with _inventory_lock:
        _inventory = []
        save_inventory()
    return jsonify({"ok": True})


@app.route("/dashboard")
def dashboard():
    return render_template("dashboard.html")


if __name__ == "__main__":
    print(f"시리얼 포트: {SERIAL_PORT} @ {BAUD_RATE} baud")
    print(f"사용 모델: {MODEL}")
    load_inventory()
    print(f"재고 {len(_inventory)}건 불러옴 ({INVENTORY_PATH})")
    try:
        get_serial()
        print("ESP32 카메라 연결 확인됨 (READY)")
    except Exception as e:
        print(f"경고: 시작 시 ESP32 연결 실패 - {e}")
    if not (os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("CLAUDE_API")):
        print("경고: .env에 API 키가 설정되어 있지 않습니다 (ANTHROPIC_API_KEY 또는 CLAUDE_API).")

    threading.Thread(target=preview_worker, daemon=True).start()

    app.run(host="127.0.0.1", port=5000, debug=False, threaded=True)
