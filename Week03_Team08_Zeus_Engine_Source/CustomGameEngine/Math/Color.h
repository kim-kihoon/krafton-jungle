#pragma once

struct FColor {
  float r, g, b, a;
  FColor(float _r = 0.f, float _g = 0.f, float _b = 0.f, float _a = 1.0f)
      : r(_r), g(_g), b(_b), a(_a) {}

  FColor operator+(float num) const;
  FColor operator+(const FColor& c) const;
  FColor operator-(float num) const;
  FColor operator-(const FColor& c) const;
  FColor operator*(float num) const;
  FColor operator*(const FColor& c) const;

  static FColor Red() { return FColor(1.f, 0.f, 0.f, 1.f); }
  static FColor Green() { return FColor(0.f, 1.f, 0.f, 1.f); }
  static FColor Blue() { return FColor(0.f, 0.f, 1.f, 1.f); }
  static FColor White() { return FColor(1.f, 1.f, 1.f, 1.f); }
  static FColor Black() { return FColor(0.f, 0.f, 0.f, 1.f); }
  static FColor Yellow() { return FColor(1.f, 1.f, 0.f, 1.f); }
};
