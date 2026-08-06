/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef SAVEBOOTSTRAP_H
#define SAVEBOOTSTRAP_H

#include <optional>

#include <QString>

#include "types.h"

class SaveManager;

namespace SaveBootstrap
{
void Initialize(const std::optional<QString>& romPath);
bool IsEnabled();
void Observe(SaveManager* save, melonDS::u32 frame);
}

#endif // SAVEBOOTSTRAP_H
