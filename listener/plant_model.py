"""Shared plant-health rules (edge on ESP32 must mirror these thresholds)."""

from __future__ import annotations

from dataclasses import dataclass
from time import perf_counter


@dataclass(frozen=True)
class PlantDecision:
    label: str
    confidence: float
    reason: str


def classify_plant(
    temp: float | None,
    humidity: float | None,
    light: float | None,
    soil: float | None,
) -> PlantDecision:
    if temp is None or soil is None:
        return PlantDecision("unknown", 0.0, "missing_temp_or_soil")

    if soil >= 3500:
        return PlantDecision("needs_water", 0.92, "soil_adc_dry")
    if soil <= 1500:
        return PlantDecision("overwatered", 0.88, "soil_adc_wet")
    if temp > 35 or temp < 10:
        return PlantDecision("temperature_stress", 0.85, "temp_out_of_range")
    if humidity is not None and humidity < 30:
        return PlantDecision("low_humidity", 0.8, "humidity_low")
    if light is not None and light < 500:
        return PlantDecision("low_light", 0.75, "ldr_low")

    return PlantDecision("healthy", 0.9, "within_thresholds")


def classify_with_timing(
    temp: float | None,
    humidity: float | None,
    light: float | None,
    soil: float | None,
) -> tuple[PlantDecision, float]:
    t0 = perf_counter()
    decision = classify_plant(temp, humidity, light, soil)
    elapsed_ms = (perf_counter() - t0) * 1000.0
    return decision, elapsed_ms
