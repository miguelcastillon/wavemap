#ifndef WAVEMAP_IO_FILE_CONVERSIONS_H_
#define WAVEMAP_IO_FILE_CONVERSIONS_H_

#include <string>

#include <wavemap/data_structure/volumetric/volumetric_data_structure_base.h>

#include "wavemap_io/stream_conversions.h"

namespace wavemap::io {
bool mapToFile(const VolumetricDataStructureBase& map,
               const std::string& file_path);
bool fileToMap(const std::string& file_path,
               VolumetricDataStructureBase::Ptr& map);
}  // namespace wavemap::io

#endif  // WAVEMAP_IO_FILE_CONVERSIONS_H_
