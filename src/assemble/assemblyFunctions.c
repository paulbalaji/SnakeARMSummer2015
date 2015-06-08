#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "definitions.h"
#include "mapStructs.h"
#include "map.h"
#include "assemblyFunctions.h"
#include "instructions.h"
#include "binaryWriter.h"
/*
 * TODO:
 */
uint32_t assembly(char **argv, uint8_t *memory) {
    FILE *fp = fopen(argv[1], "r");
    if (fp == NULL) {
        printf("Could not open input file.\n");
        exit(EXIT_FAILURE);
    }

    maps_t maps = initMaps();
    uint32_t programLength = firstPass(fp, &maps.labelMap);
    rewind(fp);
    uint32_t memoryLength = secondPass(fp, maps, memory, programLength);
    destroyMaps(maps);

    fclose(fp);
    return memoryLength;
}

/*
 * Function goes through the code checking where there are labels and
 * inserts those labels with their addresses to the map structure
 * and returns end of the code address
 */
uint32_t firstPass(FILE *fp, map_t *labelMap) {
    uint32_t address = 0;
    char buf[MAX_LINE_LENGTH];
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        preprocessLine(buf);
        if (strlen(buf) != 0) {
            if (isLabel(buf)) {
                buf[strlen(buf) - 1] = '\0';
                mapPut(labelMap, buf, address);
            } else {
                address += BYTES_IN_WORD;
            }
        }
    }
    return address;
}

/*
 * Function generates binary encoding splitting line of the code to the tokens
 * and calling given instruction set. After encoding it destroys map structures
 * and returns length of the program.
 */
uint32_t secondPass(FILE *fp, maps_t maps, uint8_t *memory,
        uint32_t programLength) {
    uint32_t address = 0;
    char buf[MAX_LINE_LENGTH];
    /*
     * Split code to the lines ignoring labels
     */
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        preprocessLine(buf);
        if (strlen(buf) != 0 && !isLabel(buf)) {
            char **tokens = tokenizer(buf);
            int typeLength = getTypeLength(tokens);
            char* type = malloc((typeLength + 1) * sizeof(char));
            strncpy(type, tokens[0], typeLength);
            type[typeLength] = '\0';
            int insType = mapGet(&maps.typeMap, type);
            /*
             * Choose instruction set depending on the route value
             */
            //TODO add a fancy enum
            uint32_t ins = 0;
            switch (insType) {
                case 0:
                    ins = dataProcessing(tokens, maps);
                    break;
                case 1:
                    ins = multiply(tokens, maps);
                    break;
                case 2:
                    ins = singleDataTransfer(tokens, maps, memory, address,
                            &programLength);
                    break;
                case 3:
                    ins = branch(tokens, maps, address);
                    break;
                case 4:
                    ins = bx(tokens, maps);
                    break;
                case 5:
                    ins = pushPop(tokens, maps);
                    break;
                default:
                    printf("Unsupported instructions type.\n");
                    break;
            }
            storeWord(memory, address, ins);
            freeTokens(tokens);
            address += BYTES_IN_WORD;
        }
    }

    return programLength;
}



void preprocessLine(char *buf) {
    buf[strlen(buf) - 1] = '\0';
    for (int i = 0; buf[i] != '\0'; i++) {
        if (buf[i] == ';' || buf[i] == '/' || buf[i] == '*') {
            buf[i] = '\0';
            break;
        }
    }
    if (strlen(buf) != 0) {
        for (int i = strlen(buf) - 1; i >= 0; i--) {
            if (buf[i] == ' ') {
                buf[i] = '\0';
            } else {
                break;
            }
        }
    }
    while (buf[0] == ' ') {
        for (int i = 0; buf[i] != '\0'; i++) {
            buf[i] = buf[i + 1];
        }
    }
}

/*
 * Function returns 1 if the given line of the code includes label
 */
int isLabel(char *buf) {
    return buf[strlen(buf) - 1] == ':';
}

/*
 * Function returns lines of code split to the tokens
 */
char **tokenizer(char *buf) {
    char **tokens = malloc(sizeof(char *) * MAX_TOKEN_LENGTH);
    if (tokens == NULL) {
        perror("tokenizer");
        exit(EXIT_FAILURE);
    }
    char copy[MAX_LINE_LENGTH];
    strcpy(copy, buf);
    char *token = strtok(copy, " ");
    for (int i = 0; i < MAX_TOKEN_LENGTH; i++) {
        if (token != NULL) {
            tokens[i] = malloc((strlen(token) + 1) * sizeof(char));
            strcpy(tokens[i], token);
            token = strtok(NULL, " ,");
        } else {
            tokens[i] = NULL;
        }
    }
    return tokens;
}

void freeTokens(char **tokens) {
    if (tokens != NULL) {
        for (int i = 0; i < MAX_TOKEN_LENGTH; i++) {
            if (tokens[i] != NULL) {
                free(tokens[i]);
            }
        }
        free(tokens);
    }
}
