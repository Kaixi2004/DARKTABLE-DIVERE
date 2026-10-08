/*
    RawSpeed - RAW file decoder.

    Copyright (C) 2025 Roman Lebedev

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "decompressors/PanasonicV8Decompressor.h"
#include "MemorySanitizer.h"
#include "adt/Array1DRef.h"
#include "adt/Array1DRefExtras.h"
#include "adt/Casts.h"
#include "adt/CroppedArray2DRef.h"
#include "adt/Invariant.h"
#include "common/RawImage.h"
#include "common/RawspeedException.h"
#include "decoders/RawDecoderException.h"
#include "fuzz/Common.h"
#include "io/Buffer.h"
#include "io/ByteStream.h"
#include "io/Endianness.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size);

namespace rawspeed {

namespace {

ByteStream getTrailingStrips(ByteStream bs, ByteStream sizesBs,
                             std::vector<Array1DRef<const uint8_t>>* out) {
  invariant(sizesBs.getRemainSize() % sizeof(uint32_t) == 0);
  uint32_t numStrips = sizesBs.getRemainSize() / sizeof(uint32_t);

  if (out)
    out->reserve(std::max(1U, numStrips));
  for (uint32_t strip = 0; strip != numStrips; ++strip) {
    const uint32_t stripSize = sizesBs.getU32();
    Buffer buf = bs.getBuffer(stripSize);
    if (out)
      out->emplace_back(buf);
  }
  invariant(sizesBs.getRemainSize() == 0);
  return bs;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
  assert(Data);

  try {
    const Buffer b(Data, implicit_cast<Buffer::size_type>(Size));
    const DataBuffer db(b, Endianness::little);
    ByteStream bs(db);

    RawImage mRaw(CreateRawImage(bs));

    uint32_t numStrips = bs.getU32();
    uint32_t numStripLineOffsets = bs.getU32();
    uint32_t numStripWidths = bs.getU32();
    uint32_t numStripHeights = bs.getU32();
    uint32_t numDefineCodesSize = bs.getU32();

    auto stripSizes = bs.getStream(numStrips, sizeof(uint32_t));
    auto stripLineOffsetsInput =
        bs.getStream(numStripLineOffsets, sizeof(uint32_t));
    auto stripWidthsInput = bs.getStream(numStripWidths, sizeof(uint16_t));
    auto stripHeightsInput = bs.getStream(numStripHeights, sizeof(uint16_t));
    auto defineCodes = bs.getStream(numDefineCodesSize);
    const auto initialPrediction = bs.getArray<uint16_t, 4>();
    const auto imgDim = bs.getArray<int, 2>();

    // The rest of the bs are the input strips.

    getTrailingStrips(bs, stripSizes, nullptr);
    // If we have not run out of tmp, we're good to proceed.
    std::vector<Array1DRef<const uint8_t>> strips;
    try {
      bs = getTrailingStrips(bs, stripSizes, &strips);
    } catch (...) {
      __builtin_unreachable();
    }
    bs = {}; // bs is no longer needed.

    if (mRaw->getDataType() != RawImageType::UINT16 ||
        mRaw->getBpp() != sizeof(uint16_t)) {
      ThrowRDE("Unexpected component count / data type");
    }

    auto stripLineOffsets =
        stripLineOffsetsInput.getVector<uint32_t>(numStripLineOffsets);
    stripLineOffsets.reserve(1); // Array1DRef does not like nullptr's.
    auto stripWidths = stripWidthsInput.getVector<uint16_t>(numStripWidths);
    stripWidths.reserve(1); // Array1DRef does not like nullptr's.
    auto stripHeights = stripHeightsInput.getVector<uint16_t>(numStripHeights);
    stripHeights.reserve(1); // Array1DRef does not like nullptr's.

    PanasonicV8Decompressor::DecompressorParamsBuilder builder(
        {imgDim[0], imgDim[1]}, initialPrediction, getAsArray1DRef(strips),
        getAsArray1DRef(stripLineOffsets), getAsArray1DRef(stripWidths),
        getAsArray1DRef(stripHeights), defineCodes);

    PanasonicV8Decompressor v8(mRaw, builder.getDecompressorParams());
    mRaw->createData();
    v8.decompress();
    MSan::CheckMemIsInitialized(mRaw->getByteDataAsUncroppedArray2DRef());
  } catch (const RawspeedException&) { // NOLINT(bugprone-empty-catch)
    // Exceptions are good, crashes are bad.
  }

  return 0;
}

} // namespace rawspeed
