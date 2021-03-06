#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H

#include "maps.h"

/*
 * Functions representing subsets of ARM instruction set
 */
uint32_t dataProcessing(char **tokens, maps_t maps);
uint32_t multiply(char **tokens, maps_t maps);
uint32_t singleDataTransfer(char **tokens, maps_t maps, uint8_t *memory,
        uint32_t address, uint32_t *memoryLength);
uint32_t branch(char **tokens, maps_t maps, uint32_t address);

int getTypeLength(char **tokens);

/*
 *Extension instructions
 */
uint32_t bx(char **tokens, maps_t maps);
uint32_t pushPop(char **tokens, maps_t maps);

#endif
