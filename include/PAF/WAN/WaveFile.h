/*
 * SPDX-FileCopyrightText: <text>Copyright 2024 Arm Limited and/or its
 * affiliates <open-source-office@arm.com></text>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of PAF, the Physical Attack Framework.
 */

#pragma once

#include "PAF/WAN/Signal.h"
#include "PAF/WAN/Waveform.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace PAF::WAN {

/// WaveFile is a base class for the different file formats supported by WAN:
/// vcd, fst, ...
class WaveFile {
  public:
    enum class FileFormat : uint8_t { UNKNOWN, VCD, FST };

    WaveFile() = delete;
    WaveFile(const WaveFile &) = delete;
    WaveFile(std::string_view filename, FileFormat Fmt)
        : fileName(filename), fileFmt(Fmt) {}

    virtual ~WaveFile();

    /// Get WaveFile format for filename.
    static FileFormat getFileFormat(std::string_view filename);

    /// Get this WaveFile format.
    [[nodiscard]] FileFormat getFileFormat() const { return fileFmt; }

    /// Get this WaveFile filename.
    [[nodiscard]] const std::string &getFileName() const { return fileName; }

    /// Convenience method to automatically detect the wavefile format and read
    /// it for read / write.
    static std::unique_ptr<WaveFile> get(std::string_view filename,
                                         bool write);

    /// Convenience method to read from a single input file.
    Waveform read();

    /// Construct a Waveform from file FileName.
    virtual bool read(Waveform &W) = 0;

    /// Save Waveform W to file 'FileName'.
    virtual bool write(const Waveform &W) = 0;

    /// Quickly read the file to collect all times with changes.
    virtual std::vector<WAN::TimeTy> getAllChangesTimes() = 0;

  protected:
    // The file name this waves are coming from.
    std::string fileName = "";
    FileFormat fileFmt;
};

Waveform readAndMerge(const std::vector<std::string> &files);

} // namespace PAF::WAN
