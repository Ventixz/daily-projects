import { test, assertEqual } from "./harness.js";
import { celsiusToFahrenheit, fahrenheitToCelsius, formatTemperature } from "../../src/core/units.js";

test("celsiusToFahrenheit: known fixed points", () => {
  assertEqual(celsiusToFahrenheit(0), 32);
  assertEqual(celsiusToFahrenheit(100), 212);
  assertEqual(celsiusToFahrenheit(-40), -40);
});

test("fahrenheitToCelsius: inverts celsiusToFahrenheit", () => {
  for (const c of [-40, -17.5, 0, 21, 37, 100]) {
    assertEqual(Math.round(fahrenheitToCelsius(celsiusToFahrenheit(c)) * 1000) / 1000, c);
  }
});

test("formatTemperature: rounds and labels celsius", () => {
  assertEqual(formatTemperature(20.4, "celsius"), "20°C");
  assertEqual(formatTemperature(20.6, "celsius"), "21°C");
});

test("formatTemperature: converts and labels fahrenheit", () => {
  assertEqual(formatTemperature(0, "fahrenheit"), "32°F");
  assertEqual(formatTemperature(100, "fahrenheit"), "212°F");
});
