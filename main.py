import asyncio
import logging
from contextlib import asynccontextmanager
from io import BytesIO
from pathlib import Path

import numpy as np
import tensorflow.keras as keras
from fastapi import FastAPI, File, UploadFile, status
from fastapi.concurrency import run_in_threadpool
from fastapi.encoders import jsonable_encoder
from fastapi.responses import JSONResponse
from PIL import Image, UnidentifiedImageError


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s | %(levelname)s | %(name)s | %(message)s",
)
logger = logging.getLogger("smart_farming_api")


# =========================
# CONFIG
# =========================

IMG_SIZE = 224

CROP_CONFIDENCE_THRESHOLD = 0.5
CROP_MARGIN_THRESHOLD = 0.15

DEFAULT_DISEASE_CONFIDENCE_THRESHOLD = 0.70
DEFAULT_DISEASE_MARGIN_THRESHOLD = 0.10

MIN_BRIGHTNESS = 35.0
MAX_BRIGHTNESS = 245.0
MIN_CONTRAST_STD = 12.0
MIN_UPLOAD_BYTES = 1024
MAX_UPLOAD_BYTES = 8 * 1024 * 1024

CROP_INFERENCE_TIMEOUT_SECONDS = 60
DISEASE_INFERENCE_TIMEOUT_SECONDS = 60

BASE_DIR = Path(__file__).resolve().parent
MODEL_PATH = BASE_DIR / "weights" / "final_model_crop_classifier.keras"

CLASS_NAMES = (
    "Cashew",
    "Cassava",
    "Maize",
    "Tomato",
    "Unknown",
)
DISEASE_MODEL_SPECS = {
    "Tomato": {
        "path": BASE_DIR / "weights" / "tomato_disease_model_epoch_15.keras",
        "class_names": (
            "Healthy",
            "Leaf Blight",
            "Leaf Curl",
            "Mosaic Virus",
            "Septoria Leaf Spot",
        ),
        "confidence_threshold": 0.65,
        "margin_threshold": 0.10,
    },
    "Maize": {
        "path": BASE_DIR / "weights" / "maize_disease_model.keras",
        "class_names": (
            "Common_Rust",
            "Grasshopper",
            "Healthy",
            "Leaf_Blight",
            "Leaf_Spot"
        ),
        "confidence_threshold": 0.65,
        "margin_threshold": 0.10,
    },
    "Cassava": {
        "path": BASE_DIR / "weights" / "cassava_disease_model.keras",
        "class_names": None,
        "confidence_threshold": DEFAULT_DISEASE_CONFIDENCE_THRESHOLD,
        "margin_threshold": DEFAULT_DISEASE_MARGIN_THRESHOLD,
    },
    "Cashew": {
        "path": BASE_DIR / "weights" / "cashew_disease_model.keras",
        "class_names": None,
        "confidence_threshold": DEFAULT_DISEASE_CONFIDENCE_THRESHOLD,
        "margin_threshold": DEFAULT_DISEASE_MARGIN_THRESHOLD,
    },
}

UNMAPPED_DISEASE_PREFIX = "__UNMAPPED_DISEASE_CLASS__"


def patch_keras_model_loading() -> None:
    dense_cls = keras.layers.Dense

    if getattr(dense_cls, "_mvp_quantization_patch", False):
        return

    original_from_config = dense_cls.from_config.__func__

    def patched_from_config(cls, config):
        config = dict(config)
        config.pop("quantization_config", None)
        return original_from_config(cls, config)

    dense_cls.from_config = classmethod(patched_from_config)
    dense_cls._mvp_quantization_patch = True


def load_inference_model(model_path: Path) -> keras.Model:
    patch_keras_model_loading()
    return keras.models.load_model(model_path, compile=False)


def safe_json_response(content: dict, status_code: int = status.HTTP_200_OK) -> JSONResponse:
    try:
        return JSONResponse(
            status_code=status_code,
            content=jsonable_encoder(content),
        )
    except Exception:
        logger.exception("JSON serialization failure")
        return JSONResponse(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            content={
                "status": "error",
                "reason": "Response serialization failure",
            },
        )


def error_response(reason: str, status_code: int) -> JSONResponse:
    logger.error("Request failed: %s", reason)
    return safe_json_response(
        {
            "status": "error",
            "reason": reason,
        },
        status_code=status_code,
    )


def rejected_response(
    reason: str,
    *,
    crop: str | None = None,
    disease: str | None = None,
    crop_confidence: float | None = None,
    disease_confidence: float | None = None,
    crop_margin: float | None = None,
    disease_margin: float | None = None,
) -> JSONResponse:
    payload = {
        "status": "rejected",
        "reason": reason,
    }

    if crop is not None:
        payload["crop"] = crop
    if disease is not None:
        payload["disease"] = disease
    if crop_confidence is not None:
        payload["crop_confidence"] = round(crop_confidence * 100, 2)
    if disease_confidence is not None:
        payload["confidence"] = round(disease_confidence * 100, 2)
        payload["disease_confidence"] = round(disease_confidence * 100, 2)
    if crop_margin is not None:
        payload["crop_margin"] = round(crop_margin, 4)
    if disease_margin is not None:
        payload["disease_margin"] = round(disease_margin, 4)

    logger.warning("Prediction rejected: %s | payload=%s", reason, payload)
    return safe_json_response(payload)


def accepted_response(
    *,
    crop: str,
    disease: str,
    crop_confidence: float,
    disease_confidence: float,
    crop_margin: float,
    disease_margin: float,
) -> JSONResponse:
    payload = {
        "crop": crop,
        "disease": disease,
        "confidence": round(disease_confidence * 100, 2),
        "crop_confidence": round(crop_confidence * 100, 2),
        "crop_margin": round(crop_margin, 4),
        "disease_margin": round(disease_margin, 4),
        "status": "accepted",
        "reason": "",
    }
    return safe_json_response(payload)


def validate_upload(file: UploadFile, image_bytes: bytes) -> str | None:
    if not image_bytes:
        return "Empty upload"

    if len(image_bytes) < MIN_UPLOAD_BYTES:
        return "Image file too small"

    if len(image_bytes) > MAX_UPLOAD_BYTES:
        return "Image file too large"

    if file.content_type and not file.content_type.startswith("image/"):
        return "Unsupported file type"

    return None


def load_and_prepare_image(image_bytes: bytes) -> tuple[np.ndarray, np.ndarray]:
    try:
        with Image.open(BytesIO(image_bytes)) as img:
            rgb_image = img.convert("RGB")
            resized_image = rgb_image.resize((IMG_SIZE, IMG_SIZE))
            image_array = np.asarray(resized_image, dtype=np.float32)
    except UnidentifiedImageError as exc:
        raise ValueError("Invalid image file") from exc
    except OSError as exc:
        raise ValueError("Corrupted image file") from exc
    except Exception as exc:
        raise ValueError("Image preprocessing failed") from exc

    batched_array = np.expand_dims(image_array, axis=0)
    return image_array, batched_array


def validate_image_quality(image_array: np.ndarray) -> str | None:
    brightness = float(np.mean(image_array))
    grayscale = np.mean(image_array, axis=2)
    contrast_std = float(np.std(grayscale))

    if brightness < MIN_BRIGHTNESS:
        return "Image too dark"

    if brightness > MAX_BRIGHTNESS:
        return "Image too bright"

    if contrast_std < MIN_CONTRAST_STD:
        return "Image has too little detail"

    return None


def run_crop_inference(model: keras.Model, batched_array: np.ndarray) -> dict:
    prediction = model.predict(batched_array, verbose=0)[0]

    predicted_index = int(np.argmax(prediction))
    confidence = float(prediction[predicted_index])

    sorted_probs = np.sort(prediction)
    margin = float(sorted_probs[-1] - sorted_probs[-2])

    predicted_class = CLASS_NAMES[predicted_index]

    if (
        confidence < CROP_CONFIDENCE_THRESHOLD
        or margin < CROP_MARGIN_THRESHOLD
        or predicted_class == "Unknown"
    ):
        return {
            "accepted": False,
            "prediction": "Unknown",
            "confidence": confidence,
            "margin": margin,
            "reason": "Low confidence or ambiguous crop prediction",
        }

    return {
        "accepted": True,
        "prediction": predicted_class,
        "confidence": confidence,
        "margin": margin,
        "reason": "Accepted",
    }


def run_specialist_inference(
    model: keras.Model,
    batched_array: np.ndarray,
    specialist_config: dict,
) -> dict:
    class_names = specialist_config["class_names"]
    prediction = model.predict(batched_array, verbose=0)[0]

    predicted_index = int(np.argmax(prediction))
    confidence = float(prediction[predicted_index])

    sorted_probs = np.sort(prediction)
    margin = float(sorted_probs[-1] - sorted_probs[-2])

    predicted_class = class_names[predicted_index]
    confidence_threshold = specialist_config.get(
        "confidence_threshold",
        DEFAULT_DISEASE_CONFIDENCE_THRESHOLD,
    )
    margin_threshold = specialist_config.get(
        "margin_threshold",
        DEFAULT_DISEASE_MARGIN_THRESHOLD,
    )

    if predicted_class.startswith(UNMAPPED_DISEASE_PREFIX):
        return {
            "accepted": False,
            "prediction": predicted_class,
            "confidence": confidence,
            "margin": margin,
            "reason": "Unmapped specialist disease class",
        }

    if confidence < confidence_threshold or margin < margin_threshold:
        return {
            "accepted": False,
            "prediction": predicted_class,
            "confidence": confidence,
            "margin": margin,
            "reason": "Low confidence disease prediction",
        }

    return {
        "accepted": True,
        "prediction": predicted_class,
        "confidence": confidence,
        "margin": margin,
        "reason": "Accepted",
    }


def align_specialist_class_names(
    crop_name: str,
    configured_names: tuple[str, ...],
    output_classes: int,
) -> tuple[tuple[str, ...], str | None]:
    configured_count = len(configured_names)

    if configured_count == output_classes:
        return configured_names, None

    if configured_count < output_classes:
        padded_names = list(configured_names)
        for idx in range(configured_count, output_classes):
            padded_names.append(f"{UNMAPPED_DISEASE_PREFIX}_{crop_name}_{idx}")

        warning = (
            f"Specialist class mapping padded for {crop_name}: "
            f"{configured_count} configured classes for {output_classes} outputs"
        )
        return tuple(padded_names), warning

    warning = (
        f"Specialist class mapping truncated for {crop_name}: "
        f"{configured_count} configured classes for {output_classes} outputs"
    )
    return tuple(configured_names[:output_classes]), warning


def load_specialist_models() -> tuple[dict, dict]:
    loaded_models = {}
    load_errors = {}
    load_warnings = {}

    for crop_name, specialist_config in DISEASE_MODEL_SPECS.items():
        model_path = specialist_config["path"]
        class_names = specialist_config.get("class_names")

        if not model_path.exists():
            reason = f"Specialist model file not found for {crop_name}: {model_path}"
            logger.warning(reason)
            load_errors[crop_name] = reason
            continue

        if not class_names:
            reason = f"Specialist class names not configured for {crop_name}"
            logger.warning(reason)
            load_errors[crop_name] = reason
            continue

        try:
            model = load_inference_model(model_path)
            dummy = np.zeros((1, IMG_SIZE, IMG_SIZE, 3), dtype=np.float32)
            model.predict(dummy, verbose=0)
            output_classes = model.output_shape[-1]

            aligned_class_names, warning = align_specialist_class_names(
                crop_name,
                tuple(class_names),
                output_classes,
            )
            specialist_config["class_names"] = aligned_class_names

            if warning:
                logger.warning(warning)
                load_warnings[crop_name] = warning

            loaded_models[crop_name] = model
            logger.info("Loaded specialist disease model for %s from %s", crop_name, model_path)
        except Exception:
            reason = f"Failed to load specialist disease model for {crop_name}"
            logger.exception(reason)
            load_errors[crop_name] = reason

    return loaded_models, load_errors, load_warnings


@asynccontextmanager
async def lifespan(app: FastAPI):
    if not MODEL_PATH.exists():
        raise RuntimeError(f"Model file not found: {MODEL_PATH}")

    model = load_inference_model(MODEL_PATH)
    dummy = np.zeros((1, IMG_SIZE, IMG_SIZE, 3), dtype=np.float32)
    model.predict(dummy, verbose=0)
    output_classes = model.output_shape[-1]

    if output_classes != len(CLASS_NAMES):
        raise RuntimeError(
            f"Model output classes ({output_classes}) do not match CLASS_NAMES ({len(CLASS_NAMES)})"
        )

    specialist_models, specialist_errors, specialist_warnings = load_specialist_models()

    app.state.model = model
    app.state.specialist_models = specialist_models
    app.state.specialist_errors = specialist_errors
    app.state.specialist_warnings = specialist_warnings

    logger.info("Crop classifier loaded from %s", MODEL_PATH)
    logger.info("Loaded specialist models for crops: %s", sorted(specialist_models.keys()))
    yield


app = FastAPI(lifespan=lifespan)


@app.get("/")
def root():
    return {"message": "Plant Classification API Running"}


@app.get("/health")
def health():
    return {
        "status": "ok",
        "model_loaded": hasattr(app.state, "model"),
        "model_path": str(MODEL_PATH),
        "classes": CLASS_NAMES,
        "loaded_specialist_models": sorted(getattr(app.state, "specialist_models", {}).keys()),
        "specialist_load_errors": getattr(app.state, "specialist_errors", {}),
        "specialist_load_warnings": getattr(app.state, "specialist_warnings", {}),
    }


@app.post("/predict")
async def predict(file: UploadFile = File(...)):
    image_bytes = await file.read()

    upload_error = validate_upload(file, image_bytes)
    if upload_error:
        return error_response(upload_error, status.HTTP_400_BAD_REQUEST)

    try:
        image_array, batched_array = load_and_prepare_image(image_bytes)
    except ValueError as exc:
        return error_response(str(exc), status.HTTP_400_BAD_REQUEST)

    quality_error = validate_image_quality(image_array)
    if quality_error:
        return rejected_response(quality_error)

    try:
        crop_result = await asyncio.wait_for(
            run_in_threadpool(
                run_crop_inference,
                app.state.model,
                batched_array,
            ),
            timeout=CROP_INFERENCE_TIMEOUT_SECONDS,
        )
    except asyncio.TimeoutError:
        logger.exception("Crop inference timed out")
        return error_response("Crop inference timed out", status.HTTP_504_GATEWAY_TIMEOUT)
    except Exception:
        logger.exception("Crop inference failure")
        return error_response("Crop inference failed", status.HTTP_500_INTERNAL_SERVER_ERROR)

    if not crop_result["accepted"]:
        logger.info(
            "Crop rejected | crop=%s | confidence=%.4f | margin=%.4f",
            crop_result["prediction"],
            crop_result["confidence"],
            crop_result["margin"],
        )
        return rejected_response(
            crop_result["reason"],
            crop=crop_result["prediction"],
            crop_confidence=crop_result["confidence"],
            crop_margin=crop_result["margin"],
        )

    crop_name = crop_result["prediction"]
    specialist_config = DISEASE_MODEL_SPECS.get(crop_name)
    specialist_model = app.state.specialist_models.get(crop_name)

    if specialist_config is None:
        logger.error("Unknown crop routing: %s", crop_name)
        return error_response(
            f"Unknown crop routing for {crop_name}",
            status.HTTP_500_INTERNAL_SERVER_ERROR,
        )

    if specialist_model is None:
        reason = app.state.specialist_errors.get(
            crop_name,
            f"Specialist model unavailable for {crop_name}",
        )
        logger.error(
            "Specialist model unavailable | crop=%s | crop_confidence=%.4f",
            crop_name,
            crop_result["confidence"],
        )
        return error_response(reason, status.HTTP_503_SERVICE_UNAVAILABLE)

    try:
        disease_result = await asyncio.wait_for(
            run_in_threadpool(
                run_specialist_inference,
                specialist_model,
                batched_array,
                specialist_config,
            ),
            timeout=DISEASE_INFERENCE_TIMEOUT_SECONDS,
        )
    except asyncio.TimeoutError:
        logger.exception("Disease inference timed out for crop %s", crop_name)
        return error_response("Disease inference timed out", status.HTTP_504_GATEWAY_TIMEOUT)
    except Exception:
        logger.exception("Disease inference failure for crop %s", crop_name)
        return error_response("Disease inference failed", status.HTTP_500_INTERNAL_SERVER_ERROR)

    if not disease_result["accepted"]:
        logger.info(
            "Disease rejected | crop=%s | disease=%s | crop_confidence=%.4f | disease_confidence=%.4f | disease_margin=%.4f",
            crop_name,
            disease_result["prediction"],
            crop_result["confidence"],
            disease_result["confidence"],
            disease_result["margin"],
        )
        return rejected_response(
            disease_result["reason"],
            crop=crop_name,
            disease=disease_result["prediction"],
            crop_confidence=crop_result["confidence"],
            disease_confidence=disease_result["confidence"],
            crop_margin=crop_result["margin"],
            disease_margin=disease_result["margin"],
        )

    logger.info(
        "Accepted prediction | crop=%s | disease=%s | crop_confidence=%.4f | disease_confidence=%.4f",
        crop_name,
        disease_result["prediction"],
        crop_result["confidence"],
        disease_result["confidence"],
    )
    return accepted_response(
        crop=crop_name,
        disease=disease_result["prediction"],
        crop_confidence=crop_result["confidence"],
        disease_confidence=disease_result["confidence"],
        crop_margin=crop_result["margin"],
        disease_margin=disease_result["margin"],
    )
