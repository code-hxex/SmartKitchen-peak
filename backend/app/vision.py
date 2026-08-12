"""초기 등록 API에서 스마트폰 사진으로부터 물건의 class/label을 인식하는 데 사용."""

import base64
import json

import anthropic

from . import config

_client: anthropic.Anthropic | None = None


def get_client() -> anthropic.Anthropic:
    global _client
    if _client is None:
        if not config.ANTHROPIC_API_KEY:
            raise RuntimeError(".env에 ANTHROPIC_API_KEY 또는 CLAUDE_API가 설정되어 있지 않습니다.")
        _client = anthropic.Anthropic(api_key=config.ANTHROPIC_API_KEY)
    return _client


REGISTRATION_SCHEMA = {
    "type": "object",
    "properties": {
        "is_fridge_item": {"type": "boolean"},
        "class_name": {"type": "string", "enum": config.FOOD_CATEGORIES},
        "label": {"type": "string", "description": "구체적인 물건 이름 (예: 서울우유 1L)"},
        "confidence": {"type": "string", "enum": ["low", "medium", "high"]},
    },
    "required": ["is_fridge_item", "class_name", "label", "confidence"],
    "additionalProperties": False,
}

PROMPT = (
    "이 사진은 냉장고에 새로 등록할 물건을 스마트폰으로 촬영한 것입니다. "
    "무엇인지 식별하고 구체적인 이름(label)과 종류(class_name)를 알려주세요."
)


def identify_for_registration(jpeg_bytes: bytes) -> dict:
    b64 = base64.standard_b64encode(jpeg_bytes).decode("utf-8")
    client = get_client()

    output_config = {"format": {"type": "json_schema", "schema": REGISTRATION_SCHEMA}}
    if "haiku" not in config.CLAUDE_MODEL:
        output_config["effort"] = "low"

    response = client.messages.create(
        model=config.CLAUDE_MODEL,
        max_tokens=512,
        output_config=output_config,
        messages=[
            {
                "role": "user",
                "content": [
                    {
                        "type": "image",
                        "source": {"type": "base64", "media_type": "image/jpeg", "data": b64},
                    },
                    {"type": "text", "text": PROMPT},
                ],
            }
        ],
    )
    text = next(b.text for b in response.content if b.type == "text")
    return json.loads(text)
