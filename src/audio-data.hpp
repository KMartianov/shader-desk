// src/audio-data.hpp

#pragma once

#include <vector>

// Единое определение структуры для передачи аудиоданных
struct AudioData {
    double volume = 0.0;
    double bass = 0.0;
    double mid = 0.0;
    double treble = 0.0;
    std::vector<double> bands;
};