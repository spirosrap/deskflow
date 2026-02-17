/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/ClipboardImageConverter.h"

#include <QBuffer>
#include <QImage>

#include <limits>

namespace deskflow::platform::clipboard {
namespace {

constexpr size_t kBmpFileHeaderSize = 14;
constexpr size_t kBmpInfoHeaderSize = 40;

uint32_t fromLEU32(const uint8_t *data)
{
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

int32_t fromLES32(const uint8_t *data)
{
  return static_cast<int32_t>(
      static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) | (static_cast<uint32_t>(data[2]) << 16) |
      (static_cast<uint32_t>(data[3]) << 24)
  );
}

uint16_t fromLEU16(const uint8_t *data)
{
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

void toLE(uint8_t *&dst, char src)
{
  dst[0] = static_cast<uint8_t>(src);
  dst += 1;
}

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

std::string addBmpFileHeader(const std::string &bitmapData)
{
  if (bitmapData.size() < kBmpInfoHeaderSize) {
    return {};
  }

  const auto *rawInfoHeader = reinterpret_cast<const uint8_t *>(bitmapData.data());
  uint32_t infoHeaderSize = fromLEU32(rawInfoHeader);
  if (infoHeaderSize < kBmpInfoHeaderSize || infoHeaderSize > bitmapData.size()) {
    return {};
  }

  uint8_t header[kBmpFileHeaderSize];
  uint8_t *dst = header;
  toLE(dst, 'B');
  toLE(dst, 'M');
  toLE(dst, static_cast<uint32_t>(kBmpFileHeaderSize + bitmapData.size()));
  toLE(dst, static_cast<uint16_t>(0));
  toLE(dst, static_cast<uint16_t>(0));
  toLE(dst, static_cast<uint32_t>(kBmpFileHeaderSize + infoHeaderSize));

  return std::string(reinterpret_cast<const char *>(header), kBmpFileHeaderSize) + bitmapData;
}

std::string stripBmpFileHeader(const std::string &bmpData)
{
  if (bmpData.size() <= kBmpFileHeaderSize + kBmpInfoHeaderSize) {
    return {};
  }

  const auto *rawBmpHeader = reinterpret_cast<const uint8_t *>(bmpData.data());
  if (rawBmpHeader[0] != 'B' || rawBmpHeader[1] != 'M') {
    return {};
  }

  uint32_t offset = fromLEU32(rawBmpHeader + 10);
  if (offset <= kBmpFileHeaderSize || offset > bmpData.size()) {
    return {};
  }

  if (offset == kBmpFileHeaderSize + kBmpInfoHeaderSize) {
    return bmpData.substr(kBmpFileHeaderSize);
  }

  return bmpData.substr(kBmpFileHeaderSize, kBmpInfoHeaderSize) + bmpData.substr(offset, bmpData.size() - offset);
}

bool isTooLargeForQt(const std::string &data)
{
  return data.size() > static_cast<size_t>(std::numeric_limits<int>::max());
}

QImage imageFromDib(const std::string &bitmapData)
{
  if (bitmapData.size() < kBmpInfoHeaderSize) {
    return {};
  }

  const auto *rawInfoHeader = reinterpret_cast<const uint8_t *>(bitmapData.data());
  const auto infoHeaderSize = static_cast<size_t>(fromLEU32(rawInfoHeader + 0));
  const auto width = fromLES32(rawInfoHeader + 4);
  const auto signedHeight = fromLES32(rawInfoHeader + 8);
  const auto planes = fromLEU16(rawInfoHeader + 12);
  const auto bitCount = fromLEU16(rawInfoHeader + 14);
  const auto compression = fromLEU32(rawInfoHeader + 16);

  if (infoHeaderSize < kBmpInfoHeaderSize || infoHeaderSize > bitmapData.size()) {
    return {};
  }
  if (width <= 0 || signedHeight == 0 || planes != 1) {
    return {};
  }
  if (compression != 0 || (bitCount != 24 && bitCount != 32)) {
    return {};
  }

  const bool topDown = signedHeight < 0;
  const int32_t height = topDown ? -signedHeight : signedHeight;
  if (height <= 0) {
    return {};
  }

  size_t rowStride = 0;
  if (bitCount == 24) {
    rowStride = static_cast<size_t>((width * 3 + 3) & ~3);
  } else {
    rowStride = static_cast<size_t>(width) * 4;
  }

  const auto pixelsOffset = infoHeaderSize;
  const auto requiredSize = pixelsOffset + rowStride * static_cast<size_t>(height);
  if (requiredSize > bitmapData.size()) {
    return {};
  }

  QImage image(width, height, QImage::Format_RGB888);
  if (image.isNull()) {
    return {};
  }

  const auto *rawPixels = reinterpret_cast<const uint8_t *>(bitmapData.data()) + pixelsOffset;
  for (int y = 0; y < height; ++y) {
    const int srcRow = topDown ? y : (height - 1 - y);
    const auto *src = rawPixels + static_cast<size_t>(srcRow) * rowStride;
    auto *dst = image.scanLine(y);
    for (int x = 0; x < width; ++x) {
      const auto srcPixel = src + (bitCount == 24 ? x * 3 : x * 4);
      dst[x * 3 + 0] = srcPixel[2];
      dst[x * 3 + 1] = srcPixel[1];
      dst[x * 3 + 2] = srcPixel[0];
    }
  }

  return image;
}

QImage imageFromData(const std::string &imageData, const char *formatHint)
{
  if (isTooLargeForQt(imageData)) {
    return {};
  }

  const auto *rawData = reinterpret_cast<const uchar *>(imageData.data());
  const auto size = static_cast<int>(imageData.size());

  if (formatHint != nullptr && *formatHint != '\0') {
    if (QImage image = QImage::fromData(rawData, size, formatHint); !image.isNull()) {
      return image;
    }
  }

  return QImage::fromData(rawData, size);
}

} // namespace

std::string encodeBitmapToImage(const std::string &bitmapData, const char *format)
{
  if (format == nullptr || *format == '\0') {
    return {};
  }

  auto image = imageFromDib(bitmapData);
  if (image.isNull()) {
    const auto bmpData = addBmpFileHeader(bitmapData);
    if (bmpData.empty()) {
      return {};
    }

    image = imageFromData(bmpData, "BMP");
    if (image.isNull()) {
      return {};
    }
  }

  auto normalized = image.convertToFormat(QImage::Format_ARGB32);
  if (normalized.isNull()) {
    return {};
  }

  // Some clipboard bitmaps carry fully transparent alpha even when RGB
  // channels are valid. Force opaque alpha to avoid black pasted images.
  bool sawNonZeroAlpha = false;
  bool sawZeroAlpha = false;
  for (int y = 0; y < normalized.height() && !(sawNonZeroAlpha && sawZeroAlpha); ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(normalized.constScanLine(y));
    for (int x = 0; x < normalized.width(); ++x) {
      if (qAlpha(row[x]) == 0) {
        sawZeroAlpha = true;
      } else {
        sawNonZeroAlpha = true;
      }
      if (sawNonZeroAlpha && sawZeroAlpha) {
        break;
      }
    }
  }

  if (sawZeroAlpha && !sawNonZeroAlpha) {
    for (int y = 0; y < normalized.height(); ++y) {
      auto *row = reinterpret_cast<QRgb *>(normalized.scanLine(y));
      for (int x = 0; x < normalized.width(); ++x) {
        row[x] = qRgba(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), 255);
      }
    }
  }

  const auto saveImage = normalized.convertToFormat(QImage::Format_RGB888);
  if (saveImage.isNull()) {
    return {};
  }

  QByteArray encoded;
  QBuffer buffer(&encoded);
  if (!buffer.open(QIODevice::WriteOnly)) {
    return {};
  }

  if (!saveImage.save(&buffer, format)) {
    return {};
  }

  return std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
}

std::string decodeImageToBitmap(const std::string &imageData, const char *formatHint)
{
  const auto image = imageFromData(imageData, formatHint);
  if (image.isNull()) {
    return {};
  }

  const auto normalized = image.convertToFormat(QImage::Format_RGB888);
  if (normalized.isNull()) {
    return {};
  }

  QByteArray bmpData;
  QBuffer buffer(&bmpData);
  if (!buffer.open(QIODevice::WriteOnly)) {
    return {};
  }

  if (!normalized.save(&buffer, "BMP")) {
    return {};
  }

  const auto bmp = std::string(bmpData.constData(), static_cast<size_t>(bmpData.size()));
  return stripBmpFileHeader(bmp);
}

} // namespace deskflow::platform::clipboard
