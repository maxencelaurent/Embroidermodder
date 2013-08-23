#include <stdio.h>
#include <stdlib.h>
#include "format-vip.h"
#include "helpers-binary.h"
#include "helpers-misc.h"
#include "emb-compress.h"

int DecodeByte(unsigned char b)
{
    if (b >= 0x80) return (-(unsigned char) (~b + 1));
    return b;
}

int DecodeStitchType(unsigned char b)
{
    switch (b)
    {
        case 0x80:
            return NORMAL;
		case 0x81:
            return TRIM;
        case 0x84:
            return STOP;
        case 0x90:
            return END;
        default:
            return NORMAL;
    }
}

unsigned char* DecompressData(unsigned char* input, int compressedInputLength, int decompressedContentLength)
{
    unsigned char* decompressedData = (unsigned char*)malloc(decompressedContentLength);
    if(!DecompressData)
    {
        return 0;
    }
    husExpand((unsigned char*)input, decompressedData, compressedInputLength, 10);
    return decompressedData;
}

typedef struct _VipHeader
{
    int magicCode;
    int numberOfStitches;
    int numberOfColors;
    short postitiveXHoopSize;
    short postitiveYHoopSize;
    short negativeXHoopSize;
    short negativeYHoopSize;
    int attributeOffset;
    int xOffset;
    int yOffset;
    unsigned char stringVal[8];
    short unknown;
    int colorLength;
} VipHeader;

int readVip(EmbPattern* pattern, const char* fileName)
{
    int fileLength;
    int i;
    unsigned char prevByte = 0;
    unsigned char *attributeData, *decodedColors, *attributeDataDecompressed;
    unsigned char *xData, *xDecompressed, *yData, *yDecompressed;
    VipHeader header;
    FILE* file = fopen(fileName, "rb");
    if(file == 0)
    {
        return 0;
    }
    fseek(file, 0x0, SEEK_END);
    fileLength = ftell(file);
    fseek(file, 0x00, SEEK_SET);
    header.magicCode = binaryReadInt32(file);
    header.numberOfStitches = binaryReadInt32(file);
    header.numberOfColors = binaryReadInt32(file);

    header.postitiveXHoopSize = binaryReadInt16(file);
    header.postitiveYHoopSize = binaryReadInt16(file);
    header.negativeXHoopSize = binaryReadInt16(file);
    header.negativeYHoopSize = binaryReadInt16(file);

    header.attributeOffset = binaryReadInt32(file);
    header.xOffset = binaryReadInt32(file);
    header.yOffset = binaryReadInt32(file);

    /*stringVal = (unsigned char*)malloc(sizeof(unsigned char)*8); TODO: review this line and uncomment or remove */
    /* TODO: malloc fail error */
    binaryReadBytes(file, header.stringVal, 8);

    header.unknown = binaryReadInt16(file);

    header.colorLength = binaryReadInt32(file);
    decodedColors = (unsigned char*)malloc(header.numberOfColors*4);
    /* TODO: malloc fail error */
    for(i = 0; i < header.numberOfColors*4; ++i)
    {
        unsigned char inputByte = binaryReadByte(file);
        unsigned char tmpByte = (unsigned char) (inputByte ^ vipDecodingTable[i]);
        decodedColors[i] = (unsigned char) (tmpByte ^ prevByte);
        prevByte = inputByte;
    }
    for(i = 0; i < header.numberOfColors; i++)
    {
        EmbThread thread;
        int startIndex = i << 2;
        thread.color.r = decodedColors[startIndex];
        thread.color.g = decodedColors[startIndex + 1];
        thread.color.b = decodedColors[startIndex + 2];
		/* printf("%d\n", decodedColors[startIndex + 3]); */
        embPattern_addThread(pattern, thread);
    }
    fseek(file, header.attributeOffset, SEEK_SET);
    attributeData = (unsigned char*)malloc(header.xOffset - header.attributeOffset);
    /* TODO: malloc fail error */
    binaryReadBytes(file, attributeData, header.xOffset - header.attributeOffset);
    attributeDataDecompressed = DecompressData(attributeData, header.xOffset - header.attributeOffset, header.numberOfStitches);

    fseek(file, header.xOffset, SEEK_SET);
    xData = (unsigned char*)malloc(header.yOffset - header.xOffset);
    /* TODO: malloc fail error */
    binaryReadBytes(file, xData, header.yOffset - header.xOffset);
    xDecompressed = DecompressData(xData, header.yOffset - header.xOffset, header.numberOfStitches);

    fseek(file, header.yOffset, SEEK_SET);
    yData = (unsigned char*)malloc(fileLength - header.yOffset);
    /* TODO: malloc fail error */
    binaryReadBytes(file, yData, fileLength - header.yOffset);
    yDecompressed = DecompressData(yData, fileLength - header.yOffset, header.numberOfStitches);

    for(i = 0; i < header.numberOfStitches; i++)
    {
        embPattern_addStitchRel(pattern, DecodeByte(xDecompressed[i]) / 10.0,
            DecodeByte(yDecompressed[i]) / 10.0, DecodeStitchType(attributeDataDecompressed[i]), 1);
    }
    embPattern_addStitchRel(pattern, 0, 0, END, 1);

    fclose(file);

    free(attributeData);
    free(xData);
    free(yData);
    free(attributeDataDecompressed);
    free(xDecompressed);
    free(yDecompressed);

    return 1;
}

unsigned char* vipCompressData(unsigned char* input, int decompressedInputSize, int* compressedSize)
{
    unsigned char* compressedData = (unsigned char*)malloc(sizeof(unsigned char)*decompressedInputSize*2);
    if(!compressedData)
    {
        return 0;
    }
    *compressedSize = husCompress(input, (unsigned long) decompressedInputSize, compressedData, 10, 0);
    return compressedData;
}

unsigned char vipEncodeByte(double f)
{
    return (unsigned char)(int)roundDouble(f);
}

unsigned char vipEncodeStitchType(int st)
{
    switch(st)
    {
        case NORMAL:
            return (0x80);
		case JUMP:
        case TRIM:
            return (0x81);
        case STOP:
            return (0x84);
        case END:
            return (0x90);
        default:
            return (0x80);
    }
}

int writeVip(EmbPattern* pattern, const char* fileName)
{
    EmbRect boundingRect;
    int stitchCount, minColors, patternColor;
    int attributeSize = 0;
    int xCompressedSize = 0;
    int yCompressedSize = 0;
    double previousX = 0;
    double previousY = 0;
    unsigned char* xValues, *yValues, *attributeValues;
    EmbStitchList* pointer;
    double xx = 0.0;
    double yy = 0.0;
    int flags = 0;
    int i = 0;
    unsigned char* attributeCompressed, *xCompressed, *yCompressed, *decodedColors, *encodedColors;
	unsigned char prevByte = 0;
	EmbThreadList *colorPointer;

    FILE* file = fopen(fileName, "wb");
    if(file == 0)
    {
        return 0;
    }

    stitchCount = embStitchList_count(pattern->stitchList);
	minColors = embThreadList_count(pattern->threadList);
	decodedColors = (unsigned char*)malloc(minColors << 2);
    if(!decodedColors) return 0;
	encodedColors = (unsigned char*)malloc(minColors << 2);
    if(encodedColors)
    {
        free(decodedColors);
        return 0;
    }
    /* embPattern_correctForMaxStitchLength(pattern, 0x7F, 0x7F); */

    patternColor = minColors;
    if(minColors > 24) minColors = 24;

    binaryWriteUInt(file, 0x0190FC5D);
    binaryWriteUInt(file, stitchCount);
    binaryWriteUInt(file, minColors);

    boundingRect = embPattern_calcBoundingBox(pattern);
    binaryWriteShort(file, (short) roundDouble(boundingRect.right * 10.0));
    binaryWriteShort(file, (short) -roundDouble(boundingRect.top * 10.0 - 1.0));
    binaryWriteShort(file, (short) roundDouble(boundingRect.left * 10.0));
    binaryWriteShort(file, (short) -roundDouble(boundingRect.bottom * 10.0 - 1.0));

    binaryWriteUInt(file, 0x38 + (minColors << 3));

    xValues = (unsigned char*)malloc(sizeof(unsigned char)*(stitchCount));
    yValues = (unsigned char*)malloc(sizeof(unsigned char)*(stitchCount));
    attributeValues = (unsigned char*)malloc(sizeof(unsigned char)*(stitchCount));
    if(xValues && yValues && attributeValues)
    {
        pointer = pattern->stitchList;
        while(pointer)
        {
            xx = pointer->stitch.xx;
            yy = pointer->stitch.yy;
            flags = pointer->stitch.flags;
            xValues[i] = vipEncodeByte((xx - previousX) * 10.0);
            previousX = xx;
            yValues[i] = vipEncodeByte((yy - previousY) * 10.0);
            previousY = yy;
            attributeValues[i] = vipEncodeStitchType(flags);
            pointer = pointer->next;
            i++;
        }
        attributeCompressed = vipCompressData(attributeValues, stitchCount, &attributeSize);
        xCompressed = vipCompressData(xValues, stitchCount, &xCompressedSize);
        yCompressed = vipCompressData(yValues, stitchCount, &yCompressedSize);

        binaryWriteUInt(file, (unsigned int) (0x38 + (minColors << 3) + attributeSize));
        binaryWriteUInt(file, (unsigned int) (0x38 + (minColors << 3) + attributeSize + xCompressedSize));
        binaryWriteUInt(file, 0x00000000);
        binaryWriteUInt(file, 0x00000000);
        binaryWriteUShort(file, 0x0000);

	    binaryWriteInt(file, minColors << 2);

	    colorPointer = pattern->threadList;

        for (i = 0; i < minColors; i++)
        {
            int byteChunk = i << 2;
		    EmbColor currentColor = colorPointer->thread.color;
		    decodedColors[byteChunk] = currentColor.r;
            decodedColors[byteChunk + 1] = currentColor.g;
            decodedColors[byteChunk + 2] = currentColor.b;
            decodedColors[byteChunk + 3] = 0x01;
		    colorPointer = colorPointer->next;
        }

        for (i = 0; i < minColors << 2; ++i)
        {
            unsigned char tmpByte = (unsigned char) (decodedColors[i] ^ vipDecodingTable[i]);
		    prevByte = (unsigned char) (tmpByte ^ prevByte);
		    binaryWriteByte(file, prevByte);
	    }
	    for (i = 0; i <= minColors; i++)
        {
            binaryWriteInt(file, 1);
        }
        binaryWriteUInt(file, 0); /* string length */
	    binaryWriteShort(file, 0);
        binaryWriteBytes(file, (char*) attributeCompressed, attributeSize);
	    binaryWriteBytes(file, (char*) xCompressed, xCompressedSize);
	    binaryWriteBytes(file, (char*) yCompressed, yCompressedSize);
    }

    if(attributeCompressed) free(attributeCompressed);
    if(xCompressed) free(xCompressed);
    if(yCompressed) free(yCompressed);

    if(attributeValues) free(attributeValues);
    if(xValues) free(xValues);
    if(yValues) free(yValues);

    if(decodedColors) free(decodedColors);
    if(encodedColors) free(encodedColors);

    fclose(file);
    return 1;
}

/* kate: bom off; indent-mode cstyle; indent-width 4; replace-trailing-space-save on; */
