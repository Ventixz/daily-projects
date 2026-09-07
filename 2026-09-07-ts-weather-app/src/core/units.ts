// Temperature is stored internally as Celsius everywhere (the API's native
// unit) and converted only at the point of display, so unit toggling is a
// pure re-render with no re-fetch and no risk of double-converting a value
// that's already been converted once.

export type TemperatureUnit = "celsius" | "fahrenheit";

export function celsiusToFahrenheit(celsius: number): number {
  return (celsius * 9) / 5 + 32;
}

export function fahrenheitToCelsius(fahrenheit: number): number {
  return ((fahrenheit - 32) * 5) / 9;
}

export function formatTemperature(celsius: number, unit: TemperatureUnit): string {
  const value = unit === "celsius" ? celsius : celsiusToFahrenheit(celsius);
  const symbol = unit === "celsius" ? "C" : "F";
  return `${Math.round(value)}°${symbol}`;
}
