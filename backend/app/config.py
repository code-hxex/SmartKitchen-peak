"""
설정값. .env는 리포지토리 루트(D:/2026-Makerthon/kitchen-test/.env)의 것을
그대로 재사용한다 (Claude API 키가 이미 거기 있음).
"""

import os

from dotenv import load_dotenv

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # backend/
_REPO_ROOT = os.path.dirname(BASE_DIR)
_ROOT_ENV_PATH = os.path.join(_REPO_ROOT, ".env")
load_dotenv(_ROOT_ENV_PATH)

DATA_DIR = os.path.join(BASE_DIR, "data")
EVENT_FRAME_DIR = os.path.join(DATA_DIR, "event_frames")
REGISTRATION_FRAME_DIR = os.path.join(DATA_DIR, "registration_frames")

DATABASE_URL = os.environ.get(
    "DATABASE_URL", f"sqlite:///{os.path.join(DATA_DIR, 'fridge.db')}"
)

ANTHROPIC_API_KEY = os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("CLAUDE_API")
CLAUDE_MODEL = os.environ.get("CLAUDE_MODEL", "claude-haiku-4-5")

# 궤적 분석 결과의 물건을 기존 DB 항목과 같은 물건으로 볼지 판단하는 코사인 유사도 임계값
EMBEDDING_MATCH_THRESHOLD = float(os.environ.get("EMBEDDING_MATCH_THRESHOLD", "0.75"))

# 가스 센서(BME680) 이상치 판단 파라미터
GAS_ANOMALY_WINDOW = int(os.environ.get("GAS_ANOMALY_WINDOW", "20"))  # 이동평균에 쓸 최근 판독 수
GAS_ANOMALY_DROP_PCT = float(os.environ.get("GAS_ANOMALY_DROP_PCT", "30"))  # 평균 대비 하락 % 임계값

# 주기적 재점검(reconcile) 파라미터
RECONCILE_INTERVAL_MINUTES = int(os.environ.get("RECONCILE_INTERVAL_MINUTES", "60"))
STALE_HOURS_THRESHOLD = float(os.environ.get("STALE_HOURS_THRESHOLD", "48"))

FOOD_CATEGORIES = ["야채", "과일", "유제품", "육류", "해산물", "음료", "소스/조미료", "기타"]
