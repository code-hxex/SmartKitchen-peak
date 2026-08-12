"""아주 가벼운 이미지 임베딩(placeholder).

실제로는 CLIP류 이미지 임베딩 모델이나 Voyage AI의 멀티모달 임베딩 API로
교체하는 것이 정석이지만, 이번 작업 범위(궤적 분석 자체는 외부 컴포넌트)에서
백엔드가 자체적으로 임베딩을 만들어야 하는 곳은 '초기 등록 API'뿐이라
별도 임베딩 모델/키 없이 바로 동작하도록 색상 히스토그램 기반 벡터를 쓴다.
같은 물건(비슷한 색/구성)의 재입고를 대략적으로 구분하는 용도로는 충분하지만
정교한 물건 재식별에는 한계가 있으므로, 실제 서비스로 갈 때는 교체가 필요하다.
"""

import io

import numpy as np
from PIL import Image

_BINS_PER_CHANNEL = 16
EMBEDDING_DIM = _BINS_PER_CHANNEL * 3  # RGB


def compute_embedding(jpeg_bytes: bytes) -> list[float]:
    img = Image.open(io.BytesIO(jpeg_bytes)).convert("RGB").resize((64, 64))
    arr = np.asarray(img, dtype=np.float32)

    channel_hists = []
    for ch in range(3):
        hist, _ = np.histogram(arr[:, :, ch], bins=_BINS_PER_CHANNEL, range=(0, 255))
        channel_hists.append(hist.astype(np.float32))
    vec = np.concatenate(channel_hists)

    norm = np.linalg.norm(vec)
    if norm > 0:
        vec = vec / norm
    return vec.tolist()


def cosine_similarity(a: list[float], b: list[float]) -> float:
    va, vb = np.array(a, dtype=np.float32), np.array(b, dtype=np.float32)
    if va.shape != vb.shape or va.size == 0:
        return 0.0
    denom = float(np.linalg.norm(va) * np.linalg.norm(vb))
    if denom == 0:
        return 0.0
    return float(np.dot(va, vb) / denom)
