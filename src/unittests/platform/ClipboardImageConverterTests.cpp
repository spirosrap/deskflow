/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/ClipboardImageConverter.h"

#include <QTest>

#include <array>
#include <cstdint>
#include <string>

namespace {

void toLE(uint8_t *&dst, uint16_t src)
{
  dst[0] = static_cast<uint8_t>(src & 0xffu);
  dst[1] = static_cast<uint8_t>((src >> 8) & 0xffu);
  dst += 2;
}

void toLE(uint8_t *&dst, uint32_t src)
{
  dst[0] = static_cast<uint8_t>(src & 0xffu);
  dst[1] = static_cast<uint8_t>((src >> 8) & 0xffu);
  dst[2] = static_cast<uint8_t>((src >> 16) & 0xffu);
  dst[3] = static_cast<uint8_t>((src >> 24) & 0xffu);
  dst += 4;
}

uint16_t fromLEU16(const uint8_t *data)
{
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t fromLEU32(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

std::string makeSampleBitmap()
{
  constexpr uint32_t width = 2;
  constexpr uint32_t height = 2;
  constexpr uint32_t headerSize = 40;
  constexpr uint32_t bitCount = 32;
  constexpr uint32_t imageSize = width * height * (bitCount / 8);

  std::array<uint8_t, headerSize + imageSize> bytes{};
  uint8_t *dst = bytes.data();

  // BITMAPINFOHEADER
  toLE(dst, headerSize);
  toLE(dst, width);
  toLE(dst, height);
  toLE(dst, static_cast<uint16_t>(1)); // planes
  toLE(dst, static_cast<uint16_t>(bitCount));
  toLE(dst, static_cast<uint32_t>(0)); // BI_RGB
  toLE(dst, imageSize);
  toLE(dst, static_cast<uint32_t>(2834)); // 72 dpi
  toLE(dst, static_cast<uint32_t>(2834)); // 72 dpi
  toLE(dst, static_cast<uint32_t>(0));
  toLE(dst, static_cast<uint32_t>(0));

  // Pixel data is bottom-up BGRA.
  // Bottom row: red, green
  *dst++ = 0x00;
  *dst++ = 0x00;
  *dst++ = 0xff;
  *dst++ = 0xff;
  *dst++ = 0x00;
  *dst++ = 0xff;
  *dst++ = 0x00;
  *dst++ = 0xff;

  // Top row: blue, white
  *dst++ = 0xff;
  *dst++ = 0x00;
  *dst++ = 0x00;
  *dst++ = 0xff;
  *dst++ = 0xff;
  *dst++ = 0xff;
  *dst++ = 0xff;
  *dst++ = 0xff;

  return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

} // namespace

class ClipboardImageConverterTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void roundTripPng();
  void decodeInvalidData();
  void encodeInvalidBitmap();
};

void ClipboardImageConverterTests::roundTripPng()
{
  const auto bitmap = makeSampleBitmap();
  const auto png = deskflow::platform::clipboard::encodeBitmapToImage(bitmap, "PNG");
  QVERIFY(!png.empty());

  const auto decoded = deskflow::platform::clipboard::decodeImageToBitmap(png, "PNG");
  QVERIFY(!decoded.empty());
  QVERIFY(decoded.size() >= 40);

  const auto *raw = reinterpret_cast<const uint8_t *>(decoded.data());
  QCOMPARE(fromLEU32(raw + 0), static_cast<uint32_t>(40));
  QCOMPARE(fromLEU32(raw + 4), static_cast<uint32_t>(2));
  QCOMPARE(fromLEU32(raw + 8), static_cast<uint32_t>(2));
  QCOMPARE(fromLEU16(raw + 12), static_cast<uint16_t>(1));
  const auto bitCount = fromLEU16(raw + 14);
  QVERIFY(bitCount == 24 || bitCount == 32);
}

void ClipboardImageConverterTests::decodeInvalidData()
{
  const auto decoded = deskflow::platform::clipboard::decodeImageToBitmap("not-an-image", "PNG");
  QVERIFY(decoded.empty());
}

void ClipboardImageConverterTests::encodeInvalidBitmap()
{
  const auto encoded = deskflow::platform::clipboard::encodeBitmapToImage("short", "PNG");
  QVERIFY(encoded.empty());
}

QTEST_MAIN(ClipboardImageConverterTests)
#include "ClipboardImageConverterTests.moc"
