﻿#include "Combo.h"

namespace Combos
{

    std::vector<Combo*> find(const char* path)
    {
        FILE* f = fopen(path, "rb");

        if (!f)
        {
            return {};
        }

        std::vector<Combo*> combos;
        char current = fgetc(f);

        while (current != -1)
        {
            auto combo = new Combo();

            char name[260] = "Unnamed Combo";

            int i = 0;
            while (current != '\0')
            {
                name[i] = current;
                current = fgetc(f);
                i++;
            }
            name[i++] = '\0';

            combo->name = name;

            int32_t sample_count = 0;
            fread(&sample_count, sizeof(int32_t), 1, f);

            combo->samples = {};
            combo->samples.resize(sample_count);

            fread(combo->samples.data(), sizeof(uint32_t), sample_count, f);


            current = fgetc(f);
        }

        return combos;
    }

    bool save(const char* path, std::vector<Combo*> combos)
    {
        FILE* f = fopen(path, "wb");

        if (!f)
        {
            return false;
        }

        for (auto& combo : combos)
        {
            fputs(combo->name.c_str(), f);
            // null-terminate the string
            fputc(0, f);

            uint32_t size = combo->samples.size();
            fwrite(&size, 4, 1, f);
            fwrite(combo->samples.data(), sizeof(BUTTONS), combo->samples.size(), f);
        }

        fclose(f);
        return true;
    }
};
