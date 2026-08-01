/*
    Copyright 2016-2026 melonDS team, RSDuck

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "../ARM.h"
#include "../NDS.h"
using namespace melonDS;
int main(int argc, char* argv[])
{
    FILE* f = fopen("ARMJIT_Offsets.h", "w");
#define writeOffset(field) \
        fprintf(f, "#define ARM_" #field "_offset 0x%x\n", offsetof(ARM, field))

    writeOffset(CPSR);
    writeOffset(Num);
    writeOffset(Cycles);
    writeOffset(StopExecution);
    fprintf(f, "#define ARM_R15_offset 0x%x\n", offsetof(ARM, R[15]));
    writeOffset(FastBlockLookupStart);
    writeOffset(FastBlockLookupSize);
    writeOffset(FastBlockLookup);
    writeOffset(JitCodeBase);
    writeOffset(NDS);
    fprintf(f, "#define NDS_ARM9Timestamp_offset 0x%x\n", offsetof(NDS, ARM9Timestamp));
    fprintf(f, "#define NDS_ARM9Target_offset 0x%x\n", offsetof(NDS, ARM9Target));

    fclose(f);
    return 0;
}
