#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <err.h>
#include <string.h>
#include <assert.h>
#include <endian.h>

#define MAX_LINE_LENGTH 512
#define BYTES_IN_WORD 4
#define BITS_IN_BYTE 8

#define INT_BASE 10

#define I_BIT 25
#define S_BIT 20
#define A_BIT 21
#define P_BIT 24
#define U_BIT 23
#define L_BIT 20

#define MULTIPLY_CONST 4
#define MULTIPLY_RD 16
#define MULTIPLY_RN 12
#define MULTIPLY_RS 8
#define MULTIPLY_RM 0

#define OFFSET_POS 0
#define COND_POS 28
#define RD_POS 12
#define OPCODE_POS 21
#define RN_POS 16
#define RM_POS 0

#define SHIFT_TYPE_POS 5
#define SHIFT_VALUE_POS 7
#define RS_POS 8
#define IMM_ROTATE_POS 8

#define MEMORY_SIZE 65536

typedef struct map_elem {
    char *string;
    int integer;
    struct map_elem *next;
} map_e;

typedef struct map {
    map_e *head;
} map_t;

map_e *mapAllocElem(void);
void mapFreeElem(map_e *elem);
void mapInit(map_t *m);
void mapDestroy(map_t *m);
void mapPut(map_t *m, char *string, int integer);
int mapGet(map_t *m, char *string);

uint32_t setBit(uint32_t ins, int pos);
uint32_t setBits(uint32_t ins, uint32_t val, int pos);

uint32_t assembly(char **argv, uint8_t *memory);
uint32_t firstPass(FILE *fp, map_t *labelMap);
uint32_t secondPass(FILE *fp, map_t *labelMap, uint32_t programLength,
        uint8_t *memory);
void writeBinary(char **argv, uint8_t *memory, uint32_t totalLength);

int isLabel(char *buf);
map_t initOpcodeMap(void);
map_t initCondMap(void);
map_t initShiftMap(void);
map_t initRoutingMap(void);
void storeWord(uint8_t *memory, uint32_t address, uint32_t word);

char **tokenizer(char *buf);

void branch(char **tokens, uint8_t *memory, uint32_t address, map_t *labelMap,
        map_t *condMap);
void multiply(char **tokens, uint8_t *memory, uint32_t address);
void sDataTrans(char **tokens, uint8_t *memory, uint32_t address,
        map_t *shiftMap, uint32_t *endProgramPtr);
void dataProcessing(char **tokens, uint8_t *memory, uint32_t address,
        map_t *opcodeMap, map_t *shiftMap);

// single data transfer helper functions
uint32_t setIndexing(char **tokens, uint32_t ins);
uint32_t setImmValue(uint32_t ins, char **tokens, uint32_t *endProgramPtr,
        uint8_t *memory, uint32_t address);
uint32_t setRnValue(char **tokens, uint32_t ins);
uint32_t setShiftType(uint32_t ins, char *shiftType);
uint32_t setShiftValue(uint32_t ins, char* shiftValue);

int main(int argc, char **argv) {

    if (argc < 3) {
        printf("Input file and/or output file not specified.\n");
        exit(EXIT_FAILURE);
    }

    uint8_t *memory = malloc(MEMORY_SIZE);
    if (memory == NULL) {
        perror("memory");
        exit(EXIT_FAILURE);
    }
    memset(memory, 0, MEMORY_SIZE);

    uint32_t totalLength = assembly(argv, memory);
    writeBinary(argv, memory, totalLength);

    free(memory);
    return 0;
}

map_e *mapAllocElem(void) {
    map_e *elem = malloc(sizeof(map_e));
    if (elem == NULL) {
        perror("newListElem");
        exit(EXIT_FAILURE);
    }
    return elem;
}

void mapFreeElem(map_e *elem) {
    free(elem);
}

void mapInit(map_t *m) {
    m->head = NULL;
}

void mapDestroy(map_t *m) {
    map_e *elem = m->head;
    while (elem != NULL) {
        map_e *next = elem->next;
        mapFreeElem(elem);
        elem = next;
    }
}

void mapPut(map_t *m, char *string, int integer) {
    map_e *elem = mapAllocElem();
    elem->string = malloc(strlen(string) * sizeof(char)); //TODO free this
    strcpy(elem->string, string);
    elem->integer = integer;
    elem->next = m->head;
    m->head = elem;
}

int mapGet(map_t *m, char *string) {
    map_e *elem = m->head;
    while (strcmp(elem->string, string)) {
        elem = elem->next;
    }
    return elem->integer;
}

uint32_t setBit(uint32_t ins, int pos) {
    return ins | (1 << pos);
}

uint32_t setBits(uint32_t ins, uint32_t val, int pos) {
    return ins | (val << pos);
}

uint32_t assembly(char **argv, uint8_t *memory) {
    FILE *fp = fopen(argv[1], "r");
    if (fp == NULL) {
        printf("Could not open input file.\n");
        exit(EXIT_FAILURE);
    }

    map_t labelMap;
    mapInit(&labelMap);

    uint32_t programLength = firstPass(fp, &labelMap);
    fclose(fp);
    fp = fopen(argv[1], "r"); //TODO HACKY FIND BETTER WAY

    uint32_t totalLength = secondPass(fp, &labelMap, programLength, memory);

    mapDestroy(&labelMap);

    fclose(fp);
    return totalLength;
}

uint32_t firstPass(FILE *fp, map_t *labelMap) {
    uint32_t address = 0;
    char buf[MAX_LINE_LENGTH];
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        if (isLabel(buf)) {
            buf[strlen(buf) - 2] = '\0';
            mapPut(labelMap, buf, address);
        } else {
            if (strcmp("", buf) && strcmp("\n", buf)) {
                address += BYTES_IN_WORD;
            }

        }
    }
    return address;
}

uint32_t secondPass(FILE *fp, map_t *labelMap, uint32_t programLength,
        uint8_t *memory) {
    map_t opcodeMap = initOpcodeMap();
    map_t condMap = initCondMap();
    map_t shiftMap = initShiftMap();
    map_t routingMap = initRoutingMap();
    uint32_t address = 0;

    char buf[MAX_LINE_LENGTH];
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        if (!isLabel(buf)) {

            char **tokens = tokenizer(buf);
            if (tokens[0] == NULL) {
                free(tokens);

                continue;
            }
            int route = mapGet(&routingMap, tokens[0]);

            //TODO add a fancy enum
            switch (route) {
                case 0:
                    dataProcessing(tokens, memory, address, &opcodeMap,
                            &shiftMap);
                    break;
                case 1:
                    multiply(tokens, memory, address);
                    break;
                case 2:
                    sDataTrans(tokens, memory, address, &shiftMap,
                            &programLength);
                    break;
                case 3:
                    branch(tokens, memory, address, labelMap, &condMap);
                    break;
                default:
                    printf("Unsupported opcode \n");
                    break;
            }

            free(tokens);
            address += BYTES_IN_WORD;
        }
    }

    mapDestroy(&opcodeMap);
    mapDestroy(&condMap);
    mapDestroy(&shiftMap);

    return programLength;
}

void writeBinary(char **argv, uint8_t *memory, uint32_t totalLength) {
    FILE *fp = fopen(argv[2], "wb");
    if (fp == NULL) {
        printf("Could not open output file.\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < totalLength; i++) {
        fwrite(&memory[i], sizeof(uint8_t), 1, fp);
    }

    fclose(fp);
}

int isLabel(char *buf) {
    return buf[strlen(buf) - 2] == ':';
}

map_t initRoutingMap(void) {
    map_t routingMap;
    mapInit(&routingMap);
    mapPut(&routingMap, "and", 0);
    mapPut(&routingMap, "eor", 0);
    mapPut(&routingMap, "sub", 0);
    mapPut(&routingMap, "rsb", 0);
    mapPut(&routingMap, "add", 0);
    mapPut(&routingMap, "orr", 0);
    mapPut(&routingMap, "mov", 0);
    mapPut(&routingMap, "tst", 0);
    mapPut(&routingMap, "teq", 0);
    mapPut(&routingMap, "cmp", 0);

    mapPut(&routingMap, "mul", 1);
    mapPut(&routingMap, "mla", 1);

    mapPut(&routingMap, "ldr", 2);
    mapPut(&routingMap, "str", 2);

    mapPut(&routingMap, "b", 3);
    mapPut(&routingMap, "ble", 3);
    mapPut(&routingMap, "bgt", 3);
    mapPut(&routingMap, "blt", 3);
    mapPut(&routingMap, "bge", 3);
    mapPut(&routingMap, "bne", 3);
    mapPut(&routingMap, "beq", 3);

    mapPut(&routingMap, "lsl", 0);
    mapPut(&routingMap, "andeq", 0);

    return routingMap;

}

map_t initOpcodeMap(void) {
    map_t opcodeMap;
    mapInit(&opcodeMap);
    mapPut(&opcodeMap, "and", 0x0);
    mapPut(&opcodeMap, "eor", 0x1);
    mapPut(&opcodeMap, "sub", 0x2);
    mapPut(&opcodeMap, "rsb", 0x3);
    mapPut(&opcodeMap, "add", 0x4);
    mapPut(&opcodeMap, "orr", 0xc);
    mapPut(&opcodeMap, "mov", 0xd);
    mapPut(&opcodeMap, "tst", 0x8);
    mapPut(&opcodeMap, "teq", 0x9);
    mapPut(&opcodeMap, "cmp", 0xa);
    return opcodeMap;
}

map_t initCondMap(void) {
    map_t condMap;
    mapInit(&condMap);
    mapPut(&condMap, "eq", 0x0);
    mapPut(&condMap, "ne", 0x1);
    mapPut(&condMap, "ge", 0xa);
    mapPut(&condMap, "lt", 0xb);
    mapPut(&condMap, "gt", 0xc);
    mapPut(&condMap, "le", 0xd);
    mapPut(&condMap, "al", 0xe);
    mapPut(&condMap, "", 0xe);
    return condMap;
}

map_t initShiftMap(void) {
    map_t shiftMap;
    mapInit(&shiftMap);
    mapPut(&shiftMap, "lsl", 0x0);
    mapPut(&shiftMap, "lsr", 0x1);
    mapPut(&shiftMap, "asr", 0x2);
    mapPut(&shiftMap, "ror", 0x3);
    return shiftMap;
}

void storeWord(uint8_t *memory, uint32_t address, uint32_t word) {
    for (int i = 0; i < BYTES_IN_WORD; i++) {
        memory[address + i] = (uint8_t) word;
        word >>= BITS_IN_BYTE;
    }
}

char **tokenizer(char *buf) {
    char **tokens = malloc(sizeof(char *) * 10);
    memset(tokens, 0, sizeof(char *) * 10);
    if (tokens == NULL) {
        perror("tokenizer");
        exit(EXIT_FAILURE);
    }
    char copy[MAX_LINE_LENGTH];
    strcpy(copy, buf);
    char *token = strtok(buf, " \n");
    int i = 0;
    while (token != NULL) {
        tokens[i] = token;
        token = strtok(NULL, " ,\n");
        i++;
    }
    return tokens;
}

void branch(char **tokens, uint8_t *memory, uint32_t address, map_t *labelMap,
        map_t *condMap) {
    uint32_t ins = 0;

    //strcmp returns 0 if there's a match, 1 if no match
    //0 is false, hence !0 indicates there's a match
    //trying to work out what cond should be
    char *condStr = tokens[0];
    condStr += 1;
    uint32_t cond = mapGet(condMap, condStr);

    //for bits 27-24
    uint32_t constant = 0xa;

    //calculate the offset
    uint32_t next_addr = mapGet(labelMap, tokens[1]);
    int32_t offset = next_addr - (address + 8);

    offset >>= 2;

    //set cond
    ins = ins | cond << 28;

    //set constant
    ins = ins | constant << 24;

    //set offset
    offset &= 0xffffff; // make offset 24bit

    ins = ins | offset;

    storeWord(memory, address, ins);
}

void multiply(char **tokens, uint8_t *memory, uint32_t address) {
    uint32_t ins = 0;
    if (!strcmp(tokens[0], "mla")) {
        //mla
        ins = setBits(ins, 1, A_BIT);
        int rn = strtol(tokens[4] + 1, NULL, INT_BASE);
        ins = ins | rn << MULTIPLY_RN;
    }
    int rs = strtol(tokens[3] + 1, NULL, INT_BASE);
    int rm = strtol(tokens[2] + 1, NULL, INT_BASE);
    int rd = strtol(tokens[1] + 1, NULL, INT_BASE);
    ins = ins | rs << MULTIPLY_RS;
    ins = ins | rm << MULTIPLY_RM;
    ins = ins | rd << MULTIPLY_RD;
    int cond = 0xe;
    ins = ins | cond << COND_POS;
    int constField = 9;
    ins = ins | constField << MULTIPLY_CONST;
    storeWord(memory, address, ins);

}

//uint32_t sDataTrans(char **tokens, uint32_t *constAdress)
void sDataTrans(char **tokens, uint8_t *memory, uint32_t address,
        map_t *shiftMap, uint32_t *endProgramPtr) {
    uint32_t ins = 0;

    //Set post/pre Indexing bit
    ins = setIndexing(tokens, ins);

    // set value of Rd
    int rd = strtol(tokens[1] + 1, NULL, 0);
    ins = setBits(ins, rd, RD_POS);
    ins = setBits(ins, 0x1, 26); //TODO make in const
    // set instruction type
    int isLoad = 0;
    if (!strcmp(tokens[0], "ldr")) {
        ins = setBits(ins, 1, L_BIT);
        isLoad = 1;
    }

    // address as a numeric constant form
    if (isLoad == 1) {
        //Immediate value expression
        if (tokens[2][0] == '=') {
            ins = setImmValue(ins, tokens, endProgramPtr, memory, address);
        }
    }

    int setU = 1;
    ins = setRnValue(tokens, ins);
    int rm = 0;
    int iBitValue = 0;

    if (tokens[3] == NULL) {
        // set offset to 0
        ins = setBits(ins, 0, OFFSET_POS);
    } else if (tokens[3][0] == '#') {
        // set offset to value of expression
        int expValue = strtol(tokens[3] + 1, NULL, 0);

        if (expValue < 0) {
            ins = setBits(ins, -expValue, OFFSET_POS);
            setU = 0;
        } else {
            ins = setBits(ins, expValue, OFFSET_POS);
        }
    } else {
        // if offset of base register is represented by a register
        if (tokens[3][0] == 'r') {
            rm = strtol(tokens[3] + 1, NULL, 0);
        } else {
            rm = strtol(tokens[3] + 2, NULL, 0);
        }
        if (tokens[3][0] != '-') {
            setU = 1;
        }
        // shift cases
        if (tokens[4] != NULL) {
            char *from = tokens[4];
            char shiftType[4];
            strncpy(shiftType, from, 3); // takes first 3 chars from token line
            shiftType[4] = '\0';
            char *shiftValue = strdup(from + 4); // takes chars from position 4
            ins = setShiftType(ins, shiftType);
            ins = setShiftValue(ins, shiftValue);
        }
        iBitValue = 1;

    }

    ins = setBits(ins, 0xe, COND_POS);
    ins = setBits(ins, iBitValue, I_BIT);
    ins = setBits(ins, setU, U_BIT);
    ins = setBits(ins, rm, RM_POS);
    ins = setBits(ins, 0, 5);
    storeWord(memory, address, ins);

}

uint32_t setIndexing(char **tokens, uint32_t ins) {
    int postIndexing = 0;
    int i = 0;
    //Check if closing bracket is in tokens[2]
    while (postIndexing == 0 && tokens[2][i] != '\0') {
        if (tokens[2][i] == ']') {
            postIndexing = 1;
        }
        i++;
    }
    if (!postIndexing || tokens[3] == NULL) {
        ins = setBits(ins, 1, P_BIT);
    }
    return ins;
}

uint32_t setImmValue(uint32_t ins, char **tokens, uint32_t *endProgramPtr,
        uint8_t *memory, uint32_t address) {

    uint32_t immValue = strtol(tokens[2] + 1, NULL, 0);
    if (immValue <= 0xff) {
        ins = setBits(ins, 1, I_BIT);
        ins = setBits(ins, 0xd, OPCODE_POS);
        ins = setBits(ins, immValue, OFFSET_POS);
        ins = ins & 0xfbefffff; //Unset LBIT and bit 26
        return ins;
    } else {
        storeWord(memory, *endProgramPtr, immValue);

        //TODO quite hacky, maybe rewrite
        //Alter tokens so it works
        tokens[2] = "[r15]";
        int offset = *endProgramPtr - address - 8;
        char stringOffset[50]; //TODO find how big this needs to be
        sprintf(stringOffset, "%d", offset);
        char* buf = (char*) malloc(sizeof(char) * 51);

        strcat(buf, "#");
        strcat(buf, stringOffset);
        *endProgramPtr += 4;
        tokens[3] = buf;
        return ins;
    }
}

uint32_t setRnValue(char **tokens, uint32_t ins) {
    int rn = 0;

    rn = strtol(tokens[2] + 2, NULL, 0);

    ins = setBits(ins, rn, RN_POS);
    return ins;
}

uint32_t setShiftType(uint32_t ins, char *shiftType) {
    int shiftBits = 0;
    if (!strcmp(shiftType, "lsl")) {
        shiftBits = 0x0; //00
    } else if (!strcmp(shiftType, "lsr")) {
        shiftBits = 0x1; //01
    } else if (!strcmp(shiftType, " asr")) {
        shiftBits = 0x2; //10
    } else if (!strcmp(shiftType, " ror")) {
        shiftBits = 0x3; //11
    }
    ins = setBits(ins, shiftBits, 5);
    return ins;
}

uint32_t setShiftValue(uint32_t ins, char* shiftValue) {
    if (shiftValue[0] == '#') {
        int shiftInt = strtol(shiftValue + 1, NULL, 0);
        assert(shiftInt < 16);
        ins = setBits(ins, shiftInt, 7);
    } else {
        int rs = strtol(shiftValue + 1, NULL, 0);
        ins = setBits(ins, rs, 8);
        ins = setBits(ins, 1, 4);
    }
    return ins;
}

void dataProcessing(char **tokens, uint8_t *memory, uint32_t address,
        map_t *opcodeMap, map_t *shiftMap) {
    if (!strcmp(tokens[0], "lsl")) {
        tokens[3] = tokens[0];
        tokens[4] = tokens[2];
        tokens[0] = "mov";
        tokens[2] = tokens[1];
    }
    uint32_t ins = 0;
    ins |= 0xe << COND_POS;
    uint32_t opcode = 0;
    uint32_t rd = 0;
    uint32_t rn = 0;
    int isS = 0;
    int op2 = 3;

    if (!strcmp(tokens[0], "and")) {
        opcode = 0x0;
        rd = strtol(tokens[1] + 1, NULL, 0);
        rn = strtol(tokens[2] + 1, NULL, 0);
    } else if (!strcmp(tokens[0], "eor")) {
        opcode = 0x1;
        rd = strtol(tokens[1] + 1, NULL, 0);
        rn = strtol(tokens[2] + 1, NULL, 0);
    } else if (!strcmp(tokens[0], "sub")) {
        opcode = 0x2;
        rd = strtol(tokens[1] + 1, NULL, 0);
        rn = strtol(tokens[2] + 1, NULL, 0);
    } else if (!strcmp(tokens[0], "rsb")) {
        opcode = 0x3;
        rd = strtol(tokens[1] + 1, NULL, 0);
        rn = strtol(tokens[2] + 1, NULL, 0);
    } else if (!strcmp(tokens[0], "add")) {
        opcode = 0x4;
        rd = strtol(tokens[1] + 1, NULL, 0);
        rn = strtol(tokens[2] + 1, NULL, 0);
    } else if (!strcmp(tokens[0], "orr")) {
        opcode = 0xc;
        rd = strtol(tokens[1] + 1, NULL, 0);
        rn = strtol(tokens[2] + 1, NULL, 0);
    } else if (!strcmp(tokens[0], "mov")) {
        opcode = 0xd;
        rd = strtol(tokens[1] + 1, NULL, 0);
        op2 = 2;
    } else if (!strcmp(tokens[0], "tst")) {
        opcode = 0x8;
        rn = strtol(tokens[1] + 1, NULL, 0);
        isS = 1;
        op2 = 2;
    } else if (!strcmp(tokens[0], "teq")) {
        opcode = 0x9;
        rn = strtol(tokens[1] + 1, NULL, 10);
        printf("%s \n", tokens[1] + 1);
        isS = 1;
        op2 = 2;
    } else if (!strcmp(tokens[0], "cmp")) {
        opcode = 0xa;
        rn = strtol(tokens[1] + 1, NULL, 0);
        isS = 1;
        op2 = 2;
    } else if (!strcmp(tokens[0], "andeq")) {
        storeWord(memory, address, 0x00000000);
        return;
    }
    ins |= opcode << OPCODE_POS;
    ins |= rd << RD_POS;

    ins |= rn << RN_POS;
    ins |= isS << S_BIT;
    if (tokens[op2][0] == '#') {
        uint32_t immValue = strtol(tokens[op2] + 1, NULL, 0);
        uint32_t shiftValue = 0;
        int isRepresentable = 0;
        while (!isRepresentable && shiftValue <= 30) {
            if (immValue <= 0xFF) {
                isRepresentable = 1;
            } else {
                //Note for Yifan - I have changed the ror here to rol
                immValue = (immValue << 2) | (immValue >> 30);
                shiftValue += 2;

            }
        }
        if (!isRepresentable) {
            printf("Error: not representable");
        } else {
            ins = setBits(ins, 0x1, I_BIT);
            ins |= immValue;
            ins |= (shiftValue / 2) << IMM_ROTATE_POS;
        }
    } else {
        uint32_t rm = strtol(tokens[op2] + 1, NULL, 0);
        ins |= rm << RM_POS;
        uint32_t shift = 0;
        if (tokens[op2 + 1] != NULL) {
            if (!strcmp(tokens[op2 + 1], "lsl")) {
                shift = 0;
            } else if (!strcmp(tokens[op2 + 1], "lsr")) {
                shift = 1;
            } else if (!strcmp(tokens[op2 + 1], "asr")) {
                shift = 2;
            } else if (!strcmp(tokens[op2 + 1], "ror")) {
                shift = 3;
            }
            ins |= shift << SHIFT_TYPE_POS;
            if (tokens[op2 + 2] != NULL) {
                if (tokens[op2 + 2][0] == '#') {
                    uint32_t shiftValue = strtol(tokens[op2 + 2] + 1, NULL, 0);
                    ins |= shiftValue << SHIFT_VALUE_POS;
                } else {
                    uint32_t rs = strtol(tokens[op2 + 2] + 1, NULL, 0);
                    ins |= rs << RS_POS;
                    ins = setBits(ins, 0x1, 4); // TODO make it a constant
                }
            }
        }
    }
    storeWord(memory, address, ins);
}
