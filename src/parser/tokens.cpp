#include "parser/tokens.hpp"

std::string tokenToString(TokenType t)
{
    switch(t)
    {
    case tNONE:
        return "tNONE";
    case tAND:
        return "tAND";
    case tBREAK:
        return "tBREAK";
    case tCOLON_EQUAL:
        return "tCOLON_EQUAL";
    case tCONCAT:
        return "tCONCAT";
    case tDARROW:
        return "tDARROW";
    case tDATA:
        return "tDATA";
    case tDCOLON:
        return "tDCOLON";
    case tDEDENT:
        return "tDEDENT";
    case tDEF:
        return "tDEF";
    case tDIV_EQUAL:
        return "tDIV_EQUAL";
    case tDOT_BRACKET:
        return "tDOT_BRACKET";
    case tELIF:
        return "tELIF";
    case tELSE:
        return "tELSE";
    case tEOF:
        return "tEOF";
    case tEOL:
        return "tEOL";
    case tEQUALS:
        return "tEQUALS";
    case tFALSE:
        return "tFALSE";
    case tFOR:
        return "tFOR";
    case tFOREIGN:
        return "tFOREIGN";
    case tFOREVER:
        return "tFOREVER";
    case tGE:
        return "tGE";
    case tIF:
        return "tIF";
    case tIN:
        return "tIN";
    case tINDENT:
        return "tINDENT";
    case tINT_LIT:
        return "tINT_LIT";
    case tLE:
        return "tLE";
    case tLET:
        return "tLET";
    case tLIDENT:
        return "tLIDENT";
    case tMATCH:
        return "tMATCH";
    case tMINUS_EQUAL:
        return "tMINUS_EQUAL";
    case tMOD:
        return "tMOD";
    case tNE:
        return "tNE";
    case tOR:
        return "tOR";
    case tPLUS_EQUAL:
        return "tPLUS_EQUAL";
    case tRARROW:
        return "tRARROW";
    case tRETURN:
        return "tRETURN";
    case tSTRING_LIT:
        return "tSTRING_LIT";
    case tSTRUCT:
        return "tSTRUCT";
    case tTIMES_EQUAL:
        return "tTIMES_EQUAL";
    case tTO:
        return "tTO";
    case tTRUE:
        return "tTRUE";
    case tTYPE:
        return "tTYPE";
    case tUIDENT:
        return "tUIDENT";
    case tVAR:
        return "tVAR";
    case tWHILE:
        return "tWHILE";
    case tWHITESPACE:
        return "tWHITESPACE";
    default:
        char c = (char)t;
        return std::string(1, c);
    }
}