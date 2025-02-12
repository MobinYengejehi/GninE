/*****************************************************************************
 *
 *  PROJECT:     GninE v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/lua/CLuaArgument.cpp
 *  PURPOSE:     Lua argument handler class
 *
 *  GninE is available from http://www.gnine.com/
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CLuaArgument.h"
#include "CLuaArguments.h"
#include "LuaCommon.h"
#include "CGame.h"
#include "CElementIDs.h"
#include "CScriptDebugging.h"
#include "CResourceManager.h"
#include "CScriptArgReader.h"

#include "wasm/CWebAssemblyVariable.h"
#include "wasm/CWebAssemblyContext.h"

extern CGame* g_pGame;

#ifndef VERIFY_ELEMENT
    #define VERIFY_ELEMENT(element) (g_pGame->GetMapManager()->GetRootElement()->IsMyChild(element, true) && !element->IsBeingDeleted())
#endif

#ifndef lua_toresource
    #define lua_toresource(luaVM, index) \
        (g_pGame->GetResourceManager()->GetResourceFromScriptID(reinterpret_cast<unsigned long>((CResource*)lua_touserdata(luaVM, index))))
#endif

using namespace std;

CCallable::CCallable()
{
    Drop();
}

CCallable::CCallable(CWebAssemblyFunction* function)
{
    Drop();

    SetWasmFunction(function);

    SetIsWasmFunctionState(false);
}

CCallable::CCallable(CResource* resource, int luaFunctionRef)
{
    Drop();

    SetLuaResource(resource);
    SetLuaFunctionRef(luaFunctionRef);

    SetIsWasmFunctionState(false);
}

bool CCallable::Call(CLuaArguments* args, CLuaArguments* results, SString* errorMessage) const
{
    if (m_bIsWasmFunction)
    {
        if (!m_pWasmFunction)
        {
            return false;
        }

        CWebAssemblyVariables wArgs, wResults;
        CWebAssemblyMemory*   memory = m_pWasmFunction->GetApiEnviornment()->script->GetMemory();

        std::vector<CWebAssemblyMemoryPointerAddress> ptrToDelete;

        if (args)
        {
            args->WriteWebAssemblyVariables(&wArgs, &m_pWasmFunction->GetFunctionType().GetArguments(), memory, &ptrToDelete);
        }

        bool callResult = m_pWasmFunction->Call(&wArgs, &wResults, errorMessage);

        if (results)
        {
            results->ReadWebAssemblyVariables(&wResults);
        }

        if (!ptrToDelete.empty())
        {
            for (const CWebAssemblyMemoryPointerAddress& ptr : ptrToDelete)
            {   
                memory->Free(ptr);
            }
        }

        return callResult;
    }

    if (!m_pLuaResource)
    {
        return false;
    }

    lua_State* luaVM = m_pLuaResource->GetVirtualMachine()->GetVM();

    if (!luaVM)
    {
        return false;
    }

    int top = lua_gettop(luaVM);

    CScriptArgReader argStream(luaVM);
    argStream.m_iIndex = top + 1;

    lua_getref(luaVM, m_iLuaFunctionRef);

    if (lua_type(luaVM, -1) != LUA_TFUNCTION)
    {
        return false;
    }

    int argsCount = 0;

    if (args)
    {
        argsCount = args->Count();

        args->PushArguments(luaVM);
    }

    bool callResult = true;

    if (lua_pcall(luaVM, argsCount, LUA_MULTRET, 0) != 0)
    {
        *errorMessage = lua_tostring(luaVM, -1);

        lua_pop(luaVM, 1);

        callResult = false;
    }

    int returnCount = lua_gettop(luaVM) - top;

    if (returnCount > 0)
    {
        if (results)
        {
            argStream.ReadLuaArguments(*results);
        }
    }

    return callResult;
}

void CCallable::WriteHash(uint8_t* hash)
{
    if (!hash)
    {
        return;
    }

    size_t headerSize = sizeof(C_CALLABLE_HASH_HEADER_BYTES) - 1;

    memcpy(hash, C_CALLABLE_HASH_HEADER_BYTES, headerSize);

    uint resourceScriptId = m_pLuaResource ? m_pLuaResource->GetScriptID() : 0;

    hash += headerSize;
    memcpy(hash, &resourceScriptId, sizeof(CWebAssemblyUserData));

    hash += sizeof(CWebAssemblyUserData);

    if (m_bIsWasmFunction)
    {
        CWebAssemblyUserData address = (CWebAssemblyUserData)m_pWasmFunction;
        memcpy(hash, &address, sizeof(CWebAssemblyUserData));
    }
    else
    {
        CWebAssemblyUserData ref = m_iLuaFunctionRef;
        memcpy(hash, &ref, sizeof(CWebAssemblyUserData));
    }

    hash += sizeof(CWebAssemblyUserData);

    uint8_t isWasmFunction = m_bIsWasmFunction;
    memcpy(hash, &isWasmFunction, sizeof(uint8_t));
}

bool CCallable::ReadHash(uint8_t* hash)
{
    if (!hash)
    {
        return false;
    }

    Drop();

    size_t headerSize = sizeof(C_CALLABLE_HASH_HEADER_BYTES) - 1;

    if (strncmp((const char*)hash, C_CALLABLE_HASH_HEADER_BYTES, headerSize) != 0)
    {
        return false;
    }

    hash += headerSize;

    uint resourceScriptId = *(CWebAssemblyUserData*)hash;

    m_pLuaResource = g_pGame->GetResourceManager()->GetResourceFromScriptID(resourceScriptId);

    if (!m_pLuaResource)
    {
        Drop();
        return false;
    }

    hash += sizeof(CWebAssemblyUserData);

    CWebAssemblyUserData functionReferenceAddress = *(CWebAssemblyUserData*)hash;

    if (functionReferenceAddress == 0)
    {
        Drop();
        return false;
    }

    hash += sizeof(CWebAssemblyUserData);

    m_bIsWasmFunction = *hash;

    if (m_bIsWasmFunction)
    {
        m_pWasmFunction = (CWebAssemblyFunction*)functionReferenceAddress;
    }
    else
    {
        m_iLuaFunctionRef = functionReferenceAddress;
    }

    return true;
}

void CCallable::Drop()
{
    m_bIsWasmFunction = false;
    m_iLuaFunctionRef = 0;
    m_pLuaResource = NULL;
    m_pWasmFunction = NULL;
}

void CCallable::SetLuaResource(CResource* resource)
{
    m_pLuaResource = resource;
}

CResource* CCallable::GetLuaResource() const
{
    return m_pLuaResource;
}

void CCallable::SetLuaFunctionRef(int luaFunctionRef)
{
    m_iLuaFunctionRef = luaFunctionRef;
}

int CCallable::GetLuaFunctionRef() const
{
    return m_iLuaFunctionRef;
}

void CCallable::SetWasmFunction(CWebAssemblyFunction* function)
{
    m_pWasmFunction = function;
}

CWebAssemblyFunction* CCallable::GetWasmFunction() const
{
    return m_pWasmFunction;
}

void CCallable::SetIsWasmFunctionState(bool state)
{
    m_bIsWasmFunction = state;
}

bool CCallable::IsWasmFunction() const
{
    return m_bIsWasmFunction;
}


bool CCallable::operator==(CCallable target) const
{
    if (m_bIsWasmFunction != target.IsWasmFunction())
    {
        return false;
    }

    return (
        m_bIsWasmFunction ?
        (
            m_pWasmFunction == target.GetWasmFunction()
        ) :
        (
            m_pLuaResource == target.GetLuaResource() &&
            m_iLuaFunctionRef == target.GetLuaFunctionRef()
        )
    );
}

CLuaArgument::CLuaArgument()
{
    m_iType = LUA_TNIL;
    m_pTableData = NULL;
    m_pUserData = NULL;
    m_Callable = CCallable();
}

CLuaArgument::CLuaArgument(const CLuaArgument& Argument, CFastHashMap<CLuaArguments*, CLuaArguments*>* pKnownTables)
{
    // Initialize and call our = on the argument
    m_pTableData = NULL;
    m_Callable = CCallable();
    CopyRecursive(Argument, pKnownTables);
}

CLuaArgument::CLuaArgument(lua_State* luaVM, int iArgument, CFastHashMap<const void*, CLuaArguments*>* pKnownTables)
{
    // Read the argument out of the lua VM
    m_pTableData = NULL;
    m_Callable = CCallable();

    Read(luaVM, iArgument, pKnownTables);
}

CLuaArgument::~CLuaArgument()
{
    // Eventually destroy our table
    DeleteTableData();
}

void CLuaArgument::CopyRecursive(const CLuaArgument& Argument, CFastHashMap<CLuaArguments*, CLuaArguments*>* pKnownTables)
{
    // Clear the string
    m_strString = "";

    // Destroy our old tabledata if neccessary
    DeleteTableData();

#ifdef MTA_DEBUG
    // Copy over line and filename too
    m_strFilename = Argument.m_strFilename;
    m_iLine = Argument.m_iLine;
#endif

    // Set our variable equally to the copy class
    m_iType = Argument.m_iType;
    switch (m_iType)
    {
        case LUA_TBOOLEAN:
        {
            m_bBoolean = Argument.m_bBoolean;
            break;
        }

        case LUA_TLIGHTUSERDATA:
        case LUA_TUSERDATA:
        {
            m_pUserData = Argument.m_pUserData;
            break;
        }

        case LUA_TNUMBER:
        {
            m_Number = Argument.m_Number;
            break;
        }

        case LUA_TTABLE:
        {
            if (pKnownTables && (m_pTableData = MapFindRef(*pKnownTables, Argument.m_pTableData)))
            {
                m_bWeakTableRef = true;
            }
            else
            {
                m_pTableData = new CLuaArguments(*Argument.m_pTableData, pKnownTables);
                m_bWeakTableRef = false;
            }
            break;
        }

        case LUA_TSTRING:
        {
            m_strString = Argument.m_strString;
            break;
        }

        case LUA_TFUNCTION:
        {
            m_Callable = Argument.m_Callable;

            break;
        }

        default:
            break;
    }
}

const CLuaArgument& CLuaArgument::operator=(const CLuaArgument& Argument)
{
    CopyRecursive(Argument);

    // Return the given class allowing for chaining
    return Argument;
}

bool CLuaArgument::operator==(const CLuaArgument& Argument) const
{
    std::set<const CLuaArguments*> knownTables;
    return IsEqualTo(Argument, &knownTables);
}

bool CLuaArgument::operator!=(const CLuaArgument& Argument) const
{
    std::set<const CLuaArguments*> knownTables;
    return !IsEqualTo(Argument, &knownTables);
}

void CLuaArgument::Read(lua_State* luaVM, int iArgument, CFastHashMap<const void*, CLuaArguments*>* pKnownTables)
{
#ifdef MTA_DEBUG
    // Store debug data for later retrieval
    m_iLine = 0;
    m_strFilename = "";
    lua_Debug debugInfo;
    if (lua_getstack(luaVM, 1, &debugInfo))
    {
        lua_getinfo(luaVM, "nlS", &debugInfo);

        m_strFilename = debugInfo.source;
        m_iLine = debugInfo.currentline;
    }
#endif

    // Eventually delete our previous string
    m_strString = "";

    DeleteTableData();

    // Grab the argument type
    m_iType = lua_type(luaVM, iArgument);
    if (m_iType != LUA_TNONE)
    {
        // Read out the content depending on the type
        switch (m_iType)
        {
            case LUA_TNIL:
                break;

            case LUA_TBOOLEAN:
            {
                m_bBoolean = lua_toboolean(luaVM, iArgument) ? true : false;
                break;
            }

            case LUA_TTABLE:
            {
                bool isFunctionReference = false;

                if (lua_getmetatable(luaVM, iArgument))
                {
                    if (lua_type(luaVM, -1) == LUA_TTABLE)
                    {
                        lua_getfield(luaVM, -1, "__call");
                        if (lua_type(luaVM, -1) == LUA_TFUNCTION)
                        {
                            lua_CFunction caller = lua_tocfunction(luaVM, -1);
                            if (caller == CallFunction)
                            {
                                isFunctionReference = true;
                            }
                        }
                        lua_pop(luaVM, 1);
                    }
                    lua_pop(luaVM, 1);
                }

                if (isFunctionReference)
                {
                    bool isNil = true;

                    lua_getfield(luaVM, iArgument, "resource");
                    lua_getfield(luaVM, iArgument, "reference");

                    if (lua_type(luaVM, -2) == LUA_TLIGHTUSERDATA)
                    {
                        if (lua_type(luaVM, -1) == LUA_TNUMBER)
                        {
                            m_Callable = CCallable(lua_toresource(luaVM, -2), lua_tonumber(luaVM, -1));

                            isNil = false;
                            m_iType = LUA_TFUNCTION;
                        }
                        else if (lua_type(luaVM, -1) == LUA_TLIGHTUSERDATA)
                        {
                            CWebAssemblyFunction* function = (CWebAssemblyFunction*)lua_touserdata(luaVM, -1);

                            if (function)
                            {
                                m_Callable = CCallable(function);

                                m_Callable.SetLuaResource(function->GetApiEnviornment()->script->GetStoreContext()->GetResource());
                                m_Callable.SetIsWasmFunctionState(true);

                                isNil = false;
                                m_iType = LUA_TFUNCTION;
                            }
                        }
                    }
                    lua_pop(luaVM, 2);

                    if (isNil)
                    {
                        m_iType = LUA_TNIL;
                    }
                }
                else
                {
                    if (pKnownTables && (m_pTableData = MapFindRef(*pKnownTables, lua_topointer(luaVM, iArgument))))
                    {
                        m_bWeakTableRef = true;
                    }
                    else
                    {
                        m_pTableData = new CLuaArguments();
                        m_pTableData->ReadTable(luaVM, iArgument, pKnownTables);
                        m_bWeakTableRef = false;
                    }
                }
                break;
            }

            case LUA_TLIGHTUSERDATA:
            {
                m_pUserData = lua_touserdata(luaVM, iArgument);
                break;
            }

            case LUA_TUSERDATA:
            {
                m_pUserData = *((void**)lua_touserdata(luaVM, iArgument));
                break;
            }

            case LUA_TNUMBER:
            {
                m_Number = lua_tonumber(luaVM, iArgument);
                break;
            }

            case LUA_TSTRING:
            {
                // Grab the lua string and its size
                const char* szLuaString = lua_tostring(luaVM, iArgument);
                size_t      sizeLuaString = lua_strlen(luaVM, iArgument);

                // Set our string
                m_strString.assign(szLuaString, sizeLuaString);
                break;
            }

            case LUA_TFUNCTION:
            {
                lua_pushvalue(luaVM, iArgument);
                int funcRef = luaL_ref(luaVM, LUA_REGISTRYINDEX);

                CLuaMain& luaMain = lua_getownercluamain(luaVM);

                m_Callable = CCallable(luaMain.GetResource(), funcRef);

                break;
            }

            case LUA_TTHREAD:
            {
                m_iType = LUA_TNIL;
                break;
            }

            default:
            {
                m_iType = LUA_TNONE;
                break;
            }
        }
    }
}

void CLuaArgument::Push(lua_State* luaVM, CFastHashMap<CLuaArguments*, int>* pKnownTables) const
{
    // Got any type?
    if (m_iType != LUA_TNONE)
    {
        // Make sure the stack has enough room
        LUA_CHECKSTACK(luaVM, 1);

        // Push it depending on the type
        switch (m_iType)
        {
            case LUA_TNIL:
            {
                lua_pushnil(luaVM);
                break;
            }

            case LUA_TBOOLEAN:
            {
                lua_pushboolean(luaVM, m_bBoolean);
                break;
            }

            case LUA_TLIGHTUSERDATA:
            case LUA_TUSERDATA:
            {
                lua_pushuserdata(luaVM, m_pUserData);
                break;
            }

            case LUA_TNUMBER:
            {
                lua_pushnumber(luaVM, m_Number);
                break;
            }

            case LUA_TTABLE:
            {
                int* pThingy;
                if (pKnownTables && (pThingy = MapFind(*pKnownTables, m_pTableData)))
                {
                    lua_getfield(luaVM, LUA_REGISTRYINDEX, "cache");
                    lua_pushnumber(luaVM, *pThingy);
                    lua_gettable(luaVM, -2);
                    lua_remove(luaVM, -2);
                }
                else
                {
                    m_pTableData->PushAsTable(luaVM, pKnownTables);
                }
                break;
            }

            case LUA_TSTRING:
            {
                lua_pushlstring(luaVM, m_strString.c_str(), m_strString.length());
                break;
            }

            case LUA_TFUNCTION:
            {
                CResource* resource = m_Callable.GetLuaResource();

                if (resource)
                {
                    lua_newtable(luaVM); // create callable

                    lua_pushresource(luaVM, resource);
                    lua_setfield(luaVM, -2, "resource"); // setting resource field

                    if (m_Callable.IsWasmFunction())
                    {
                        lua_pushuserdata(luaVM, m_Callable.GetWasmFunction());
                    }
                    else
                    {
                        lua_pushnumber(luaVM, m_Callable.GetLuaFunctionRef());
                    }
                        
                    lua_setfield(luaVM, -2, "reference"); // setting the function reference

                    lua_pushcfunction(luaVM, CleanupFunction);
                    lua_setfield(luaVM, -2, "free"); // setting cleanup function

                    lua_newtable(luaVM);
                    lua_pushcfunction(luaVM, CallFunction);
                    lua_setfield(luaVM, -2, "__call"); // set call metamethod
                    lua_setmetatable(luaVM, -2);
                }

                break;
            }
        }
    }
}

void CLuaArgument::ReadBool(bool bBool)
{
    m_strString = "";
    DeleteTableData();
    m_iType = LUA_TBOOLEAN;
    m_bBoolean = bBool;
}

void CLuaArgument::ReadTable(CLuaArguments* table)
{
    m_strString = "";
    DeleteTableData();
    m_pTableData = new CLuaArguments(*table);
    m_bWeakTableRef = false;
    m_iType = LUA_TTABLE;
}

void CLuaArgument::ReadNumber(double dNumber)
{
    m_strString = "";
    DeleteTableData();
    m_iType = LUA_TNUMBER;
    m_Number = dNumber;
}

void CLuaArgument::ReadString(const std::string& strString)
{
    m_iType = LUA_TSTRING;
    DeleteTableData();
    m_strString = strString;
}

void CLuaArgument::ReadElement(CElement* pElement)
{
    m_strString = "";
    DeleteTableData();
    if (pElement)
    {
        m_iType = LUA_TUSERDATA;
        m_pUserData = reinterpret_cast<void*>(pElement->GetID().Value());
    }
    else
        m_iType = LUA_TNIL;
}

void CLuaArgument::ReadElementID(ElementID ID)
{
    m_strString = "";
    DeleteTableData();
    m_iType = LUA_TUSERDATA;
    m_pUserData = (void*)reinterpret_cast<unsigned int*>(ID.Value());
}

void CLuaArgument::ReadScriptID(uint uiScriptID)
{
    m_strString = "";
    DeleteTableData();
    m_iType = LUA_TUSERDATA;
    m_pUserData = reinterpret_cast<void*>(uiScriptID);
}

CElement* CLuaArgument::GetElement() const
{
    ElementID ID = TO_ELEMENTID(m_pUserData);
    return CElementIDs::GetElement(ID);
}

bool CLuaArgument::GetAsString(SString& strBuffer)
{
    switch (m_iType)
    {
        case LUA_TNIL:
            strBuffer = "nil";
            break;
        case LUA_TBOOLEAN:
            if (m_bBoolean)
                strBuffer = "true";
            else
                strBuffer = "false";
            break;
        case LUA_TTABLE:
            return false;
            break;
        case LUA_TSTRING:
            strBuffer = m_strString;
            break;
        case LUA_TUSERDATA:
            return false;
            break;
        case LUA_TNUMBER:
            if (m_Number == static_cast<int>(m_Number))
                strBuffer = SString("%d", static_cast<int>(m_Number));
            else
                strBuffer = SString("%f", m_Number);

            break;
        default:
            return false;
            break;
    }
    return true;
}

// Can't use bitStream.Version() here as it is sometimes not set
bool CLuaArgument::ReadFromBitStream(NetBitStreamInterface& bitStream, std::vector<CLuaArguments*>* pKnownTables)
{
    DeleteTableData();
    SLuaTypeSync type;

    // Read out the type
    if (bitStream.Read(&type))
    {
        // Depending on what type...
        switch (type.data.ucType)
        {
            // Nil type
            case LUA_TNIL:
            {
                m_iType = LUA_TNIL;
                break;
            }

            // Boolean type
            case LUA_TBOOLEAN:
            {
                bool bValue;
                if (bitStream.ReadBit(bValue))
                    ReadBool(bValue);
                break;
            }

            // Number type
            case LUA_TNUMBER:
            {
                if (bitStream.ReadBit())
                {
                    if (bitStream.ReadBit())
                    {
                        double dNum;
                        if (bitStream.Read(dNum))
                            ReadNumber(dNum);
                    }
                    else
                    {
                        float fNum;
                        if (bitStream.Read(fNum))
                            ReadNumber(RoundFromFloatSource(fNum));
                    }
                }
                else
                {
                    int iNum;
                    if (bitStream.ReadCompressed(iNum))
                        ReadNumber(iNum);
                }
                break;
            }

            // Table type
            case LUA_TTABLE:
            {
                m_pTableData = new CLuaArguments();
                if (!m_pTableData->ReadFromBitStream(bitStream, pKnownTables))
                    return false;
                m_bWeakTableRef = false;
                m_iType = LUA_TTABLE;
                m_pTableData->ValidateTableKeys();
                break;
            }

            // Table reference
            case LUA_TTABLEREF:
            {
                unsigned long ulTableRef;
                if (bitStream.ReadCompressed(ulTableRef))
                {
                    if (pKnownTables && ulTableRef < pKnownTables->size())
                    {
                        m_pTableData = pKnownTables->at(ulTableRef);
                        m_bWeakTableRef = true;
                        m_iType = LUA_TTABLE;
                    }
                }
                break;
            }

            // String type
            case LUA_TSTRING:
            {
                // Read out the string length
                unsigned short usLength;
                if (bitStream.ReadCompressed(usLength) && usLength > 0)
                {
                    if (!bitStream.CanReadNumberOfBytes(usLength))
                        return false;
                    // Allocate a buffer and read the string into it
                    char* szValue = new char[usLength + 1];
                    if (bitStream.Read(szValue, usLength))
                    {
                        // Put it into us
                        ReadString(std::string(szValue, usLength));
                    }

                    // Delete the buffer
                    delete[] szValue;
                }
                else
                    ReadString("");

                break;
            }

            // Long string type
            case LUA_TSTRING_LONG:
            {
                // Read out the string length
                uint uiLength;
                if (bitStream.ReadCompressed(uiLength) && uiLength > 0)
                {
                    if (!bitStream.CanReadNumberOfBytes(uiLength))
                        return false;

                    bitStream.AlignReadToByteBoundary();

                    // Allocate a buffer and read the string into it
                    char* szValue = new char[uiLength + 1];
                    assert(szValue);
                    if (bitStream.Read(szValue, uiLength))
                    {
                        // Put it into us
                        ReadString(std::string(szValue, uiLength));
                    }

                    // Delete the buffer
                    delete[] szValue;
                }
                else
                    ReadString("");

                break;
            }

            // Element type?
            case LUA_TLIGHTUSERDATA:
            case LUA_TUSERDATA:
            {
                ElementID ElementID;
                if (bitStream.Read(ElementID))
                {
                    ReadElementID(ElementID);
                }
                break;
            }
        }
    }
    else
        return false;
    return true;
}

// Can't use bitStream.Version() here as it is sometimes not set
bool CLuaArgument::WriteToBitStream(NetBitStreamInterface& bitStream, CFastHashMap<CLuaArguments*, unsigned long>* pKnownTables) const
{
    SLuaTypeSync type;

    switch (GetType())
    {
        // Nil type
        case LUA_TNIL:
        {
            type.data.ucType = LUA_TNIL;
            bitStream.Write(&type);
            break;
        }

        // Boolean type
        case LUA_TBOOLEAN:
        {
            type.data.ucType = LUA_TBOOLEAN;
            bitStream.Write(&type);

            // Write the boolean to it
            bitStream.WriteBit(GetBoolean());
            break;
        }

        // Table argument
        case LUA_TTABLE:
        {
            ulong* pThingy;
            if (pKnownTables && (pThingy = MapFind(*pKnownTables, m_pTableData)))
            {
                // Self-referencing table
                type.data.ucType = LUA_TTABLEREF;
                bitStream.Write(&type);
                bitStream.WriteCompressed(*pThingy);
            }
            else
            {
                type.data.ucType = LUA_TTABLE;
                bitStream.Write(&type);

                // Write the subtable to the bitstream
                m_pTableData->WriteToBitStream(bitStream, pKnownTables);
            }
            break;
        }

        // Number argument?
        case LUA_TNUMBER:
        {
            type.data.ucType = LUA_TNUMBER;
            bitStream.Write(&type);

            int       iNumber;
            float     fNumber;
            double    dNumber;
            EDataType dataType = GetDataTypeToUse(GetNumber(), &iNumber, &fNumber, &dNumber);
            if (dataType == DATA_TYPE_INT)
            {
                bitStream.WriteBit(false);
                bitStream.WriteCompressed(iNumber);
            }
            else if (dataType == DATA_TYPE_FLOAT)
            {
                bitStream.WriteBit(true);
                bitStream.WriteBit(false);
                bitStream.Write(fNumber);
            }
            else
            {
                bitStream.WriteBit(true);
                bitStream.WriteBit(true);
                bitStream.Write(dNumber);
            }
            break;
        }

        // String argument
        case LUA_TSTRING:
        {
            // Grab the string and its length. Is it short enough to be sendable?
            const char*    szTemp = m_strString.c_str();
            size_t         sizeTemp = m_strString.length();
            unsigned short usLength = static_cast<unsigned short>(sizeTemp);
            if (sizeTemp == usLength)
            {
                // This is a string argument
                type.data.ucType = LUA_TSTRING;
                bitStream.Write(&type);

                // Write its length
                bitStream.WriteCompressed(usLength);

                // Write the content too if it's not empty
                if (usLength > 0)
                {
                    bitStream.Write(szTemp, usLength);
                }
            }
            else
            {
                // This is a long string argument
                type.data.ucType = LUA_TSTRING_LONG;
                bitStream.Write(&type);

                // Write its length
                uint uiLength = sizeTemp;
                bitStream.WriteCompressed(uiLength);

                // Write the content too if it's not empty
                if (uiLength > 0)
                {
                    bitStream.AlignWriteToByteBoundary();
                    bitStream.Write(szTemp, uiLength);
                }
            }
            break;
        }

        // Element argument
        case LUA_TLIGHTUSERDATA:
        case LUA_TUSERDATA:
        {
            // Grab the element from this userdata pointer. Valid and has a synced element ID?
            CElement* pElement = GetElement();
            if (pElement && pElement->GetID() != INVALID_ELEMENT_ID)
            {
                // Write its ID
                type.data.ucType = LUA_TUSERDATA;
                bitStream.Write(&type);
                bitStream.Write(pElement->GetID());
            }
            else
            {
                // Jax: this just spams the script debugger, it's not really neccesary
                // LogUnableToPacketize ( "Couldn't packetize argument list, invalid element specified." );

                // Write a nil though so other side won't get out of sync
                type.data.ucType = LUA_TNIL;
                bitStream.Write(&type);
                return false;
            }

            break;
        }

        // Unpacketizable type.
        default:
        {
            // Unpacketizable
            LogUnableToPacketize("Couldn't packetize argument list, unknown type specified.");

            // Write a nil though so other side won't get out of sync
            type.data.ucType = LUA_TNIL;
            bitStream.Write(&type);
            return false;
        }
    }

    // Success
    return true;
}

void CLuaArgument::LogUnableToPacketize(const char* szMessage) const
{
#ifdef MTA_DEBUG
    if (m_strFilename.length() > 0)
    {
        g_pGame->GetScriptDebugging()->LogWarning(NULL, "%s:%d: %s\n", ConformResourcePath(m_strFilename.c_str()).c_str(), m_iLine, szMessage);
    }
    else
#endif
    {
        g_pGame->GetScriptDebugging()->LogWarning(NULL, "Unknown: %s\n", szMessage);
    }
}

void CLuaArgument::DeleteTableData()
{
    if (m_pTableData)
    {
        if (!m_bWeakTableRef)
            delete m_pTableData;
        m_pTableData = NULL;
    }
}

json_object* CLuaArgument::WriteToJSONObject(bool bSerialize, CFastHashMap<CLuaArguments*, unsigned long>* pKnownTables)
{
    switch (GetType())
    {
        case LUA_TNIL:
        {
            return json_object_new_int(0);
        }
        case LUA_TBOOLEAN:
        {
            return json_object_new_boolean(GetBoolean());
        }
        case LUA_TTABLE:
        {
            ulong* pTableId;
            if (pKnownTables && (pTableId = MapFind(*pKnownTables, m_pTableData)))
            {
                // Self-referencing table
                char szTableID[10];
                snprintf(szTableID, sizeof(szTableID), "^T^%lu", *pTableId);
                return json_object_new_string(szTableID);
            }
            else
            {
                return m_pTableData->WriteTableToJSONObject(bSerialize, pKnownTables);
            }
        }
        case LUA_TNUMBER:
        {
            int iNumber;
            if (ShouldUseInt(GetNumber(), &iNumber))
            {
                return json_object_new_int(iNumber);
            }
            else
            {
                return json_object_new_double(GetNumber());
            }
            break;
        }
        case LUA_TSTRING:
        {
            SString strTemp = GetString();
            if (strTemp.length() > 3 && strTemp[0] == '^' && strTemp[2] == '^' && strTemp[1] != '^')
            {
                // Prevent clash with how MTA stores elements, resources and table refs as strings
                strTemp[2] = '~';
            }
            if (strTemp.length() <= USHRT_MAX)
            {
                return json_object_new_string_len((char*)strTemp.c_str(), strTemp.length());
            }
            else
            {
                g_pGame->GetScriptDebugging()->LogError(NULL, "Couldn't convert argument list to JSON. Invalid string specified, limit is 65535 characters.");
            }
            break;
        }
        case LUA_TLIGHTUSERDATA:
        case LUA_TUSERDATA:
        {
            CElement*  pElement = GetElement();
            CResource* pResource = g_pGame->GetResourceManager()->GetResourceFromScriptID(reinterpret_cast<unsigned long>(GetUserData()));

            // Elements are dynamic, so storing them is potentially unsafe
            if (pElement && bSerialize)
            {
                char szElementID[10] = {0};
                snprintf(szElementID, 9, "^E^%d", (int)pElement->GetID().Value());
                return json_object_new_string(szElementID);
            }
            else if (pResource)
            {
                char szElementID[MAX_RESOURCE_NAME_LENGTH + 4] = {0};
                snprintf(szElementID, MAX_RESOURCE_NAME_LENGTH + 3, "^R^%s", pResource->GetName().c_str());
                return json_object_new_string(szElementID);
            }
            else
            {
                if (pElement)            // eg toJSON() with valid element
                    g_pGame->GetScriptDebugging()->LogError(NULL, "Couldn't convert userdata argument to JSON, elements not allowed for this function.");
                else if (!bSerialize)            // eg toJSON() with invalid element
                    g_pGame->GetScriptDebugging()->LogError(
                        NULL, "Couldn't convert userdata argument to JSON, only valid resources can be included for this function.");
                else
                    g_pGame->GetScriptDebugging()->LogError(NULL,
                                                            "Couldn't convert userdata argument to JSON, only valid elements or resources can be included.");
                return NULL;
            }
            break;
        }
        default:
        {
            g_pGame->GetScriptDebugging()->LogError(
                NULL, "Couldn't convert argument list to JSON, unsupported data type. Use Table, Nil, String, Number, Boolean, Resource or Element.");
            return NULL;
        }
    }
    return NULL;
}

char* CLuaArgument::WriteToString(char* szBuffer, int length)
{
    switch (GetType())
    {
        case LUA_TNIL:
        {
            snprintf(szBuffer, length, "0");
            return szBuffer;
        }
        case LUA_TBOOLEAN:
        {
            if (GetBoolean())
                snprintf(szBuffer, length, "true");
            else
                snprintf(szBuffer, length, "false");
            return szBuffer;
        }
        case LUA_TTABLE:
        {
            g_pGame->GetScriptDebugging()->LogError(
                NULL, "Cannot convert table to string (do not use tables as keys in tables if you want to send them over http/JSON).");
            return NULL;
        }
        case LUA_TNUMBER:
        {
            int iNumber;
            if (ShouldUseInt(GetNumber(), &iNumber))
            {
                snprintf(szBuffer, length, "%d", iNumber);
                return szBuffer;
            }
            else
            {
                snprintf(szBuffer, length, "%f", GetNumber());
                return szBuffer;
            }
            break;
        }
        case LUA_TSTRING:
        {
            const char*    szTemp = GetString().c_str();
            unsigned short usLength = static_cast<unsigned short>(strlen(szTemp));
            if (strlen(szTemp) == usLength)
            {
                snprintf(szBuffer, length, "%s", szTemp);
                return szBuffer;
            }
            else
            {
                g_pGame->GetScriptDebugging()->LogError(NULL, "String is too long. Limit is 65535 characters.");
            }
            break;
        }

        case LUA_TLIGHTUSERDATA:
        case LUA_TUSERDATA:
        {
            CElement*  pElement = GetElement();
            CResource* pResource = g_pGame->GetResourceManager()->GetResourceFromScriptID(reinterpret_cast<unsigned long>(GetUserData()));
            if (pElement)
            {
                snprintf(szBuffer, length, "#E#%d", (int)pElement->GetID().Value());
                return szBuffer;
            }
            else if (pResource)
            {
                snprintf(szBuffer, length, "#R#%s", pResource->GetName().c_str());
                return szBuffer;
            }
            else
            {
                g_pGame->GetScriptDebugging()->LogError(NULL, "Couldn't convert element to string, only valid elements can be sent.");
                return NULL;
            }
            break;
        }
        default:
        {
            g_pGame->GetScriptDebugging()->LogError(NULL,
                                                    "Couldn't convert argument to string, unsupported data type. Use String, Number, Boolean or Element.");
            return NULL;
        }
    }
    return NULL;
}

bool CLuaArgument::IsEqualTo(const CLuaArgument& compareTo, std::set<const CLuaArguments*>* knownTables) const
{
    if (m_iType != compareTo.m_iType)
        return false;

    switch (m_iType)
    {
        case LUA_TBOOLEAN:
        {
            return m_bBoolean == compareTo.m_bBoolean;
        }
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
        {
            return m_pUserData == compareTo.m_pUserData;
        }
        case LUA_TNUMBER:
        {
            return m_Number == compareTo.m_Number;
        }
        case LUA_TTABLE:
        {
            return m_pTableData->IsEqualTo(*compareTo.m_pTableData, knownTables);
        }
        case LUA_TSTRING:
        {
            return m_strString == compareTo.m_strString;
        }
        case LUA_TFUNCTION:
        {
            return m_Callable == compareTo.m_Callable;
        }
    }

    return false;
}

bool CLuaArgument::ReadFromJSONObject(json_object* object, std::vector<CLuaArguments*>* pKnownTables)
{
    DeleteTableData();

    if (!object)
        m_iType = LUA_TNIL;
    else
    {
        switch (json_object_get_type(object))
        {
            case json_type_null:
                m_iType = LUA_TNIL;
                break;
            case json_type_boolean:
                if (json_object_get_boolean(object))
                    ReadBool(true);
                else
                    ReadBool(false);
                break;
            case json_type_double:
            case json_type_int:
                ReadNumber(json_object_get_double(object));
                break;
            case json_type_object:
                m_pTableData = new CLuaArguments();
                m_pTableData->ReadFromJSONObject(object, pKnownTables);
                m_bWeakTableRef = false;
                m_iType = LUA_TTABLE;
                break;
            case json_type_array:
                m_pTableData = new CLuaArguments();
                m_pTableData->ReadFromJSONArray(object, pKnownTables);
                m_bWeakTableRef = false;
                m_iType = LUA_TTABLE;
                break;
            case json_type_string:
            {
                int     iLength = json_object_get_string_len(object);
                SString strString;
                strString.assign(json_object_get_string(object), iLength);
                if (iLength > 3 && strString[0] == '^' && strString[2] == '^' && strString[1] != '^')
                {
                    switch (strString[1])
                    {
                        case 'E':            // element
                        {
                            int       id = atoi(strString.c_str() + 3);
                            CElement* element = NULL;
                            if (id != INT_MAX && id != INT_MIN && id != 0)
                                element = CElementIDs::GetElement(id);
                            if (element)
                            {
                                ReadElement(element);
                            }
                            else
                            {
                                // Appears sometimes when a player quits
                                // g_pClientGame->GetScriptDebugging()->LogError ( NULL, "Invalid element specified in JSON string '%s'.", szString );
                                m_iType = LUA_TNIL;
                            }
                            break;
                        }
                        case 'R':            // resource
                        {
                            CResource* resource = g_pGame->GetResourceManager()->GetResource(strString.c_str() + 3);
                            if (resource)
                            {
                                ReadScriptID(resource->GetScriptID());
                            }
                            else
                            {
                                g_pGame->GetScriptDebugging()->LogError(NULL, "Invalid resource specified in JSON string '%s'.", strString.c_str());
                                m_iType = LUA_TNIL;
                            }
                            break;
                        }
                        case 'T':            // Table reference
                        {
                            unsigned long ulTableID = static_cast<unsigned long>(atol(strString.c_str() + 3));
                            if (pKnownTables && ulTableID < pKnownTables->size())
                            {
                                m_pTableData = pKnownTables->at(ulTableID);
                                m_bWeakTableRef = true;
                                m_iType = LUA_TTABLE;
                            }
                            else
                            {
                                g_pGame->GetScriptDebugging()->LogError(NULL, "Invalid table reference specified in JSON string '%s'.", strString.c_str());
                                m_iType = LUA_TNIL;
                            }
                            break;
                        }
                    }
                }
                else
                    ReadString(strString);
                break;
            }
            default:
                return false;
        }
    }
    return true;
}

void CLuaArgument::SetBoolean(bool value)
{
    m_iType = LUA_TBOOLEAN;
    m_bBoolean = value;
}

void CLuaArgument::SetNumber(lua_Number value)
{
    m_iType = LUA_TNUMBER;
    m_Number = value;
}

void CLuaArgument::SetString(std::string string)
{
    m_iType = LUA_TSTRING;
    m_strString = string;
}

void CLuaArgument::SetUserData(void* userData)
{
    m_iType = LUA_TUSERDATA;
    m_pUserData = userData;
}

void CLuaArgument::SetCallable(CCallable callable)
{
    m_iType = LUA_TFUNCTION;
    m_Callable = callable;
}

int CLuaArgument::CallFunction(lua_State* luaVM)
{
    if (lua_type(luaVM, 1) == LUA_TTABLE)
    {
        lua_getfield(luaVM, 1, "resource");

        CResource*        resource = nullptr;
        CScriptDebugging* debugger = g_pGame->GetScriptDebugging();

        if (lua_type(luaVM, -1) == LUA_TLIGHTUSERDATA)
        {
            resource = lua_toresource(luaVM, -1);
        }

        lua_pop(luaVM, 1);

        if (resource)
        {
            const char* resourceName = resource->GetName().c_str();

            if (resource->IsActive())
            {
                lua_State* resourceVM = resource->GetVirtualMachine()->GetVM();

                if (!resourceVM)
                {
                    debugger->LogCustom(luaVM, "Couldn't get resource lua virtual machine!");
                    return 0;
                }

                lua_getfield(luaVM, 1, "reference");

                CCallable callable;

                if (lua_type(luaVM, -1) == LUA_TLIGHTUSERDATA)
                {
                    CWebAssemblyFunction* wasmFunction = (CWebAssemblyFunction*)lua_touserdata(luaVM, -1);

                    if (!wasmFunction)
                    {
                        lua_pop(luaVM, 1);

                        debugger->LogCustom(luaVM, "Web assembly function is not accessable anymore!");

                        return 0;
                    }

                    callable = CCallable(wasmFunction);

                    callable.SetLuaResource(resource);
                    callable.SetIsWasmFunctionState(true);
                }
                else if (lua_type(luaVM, -1) == LUA_TNUMBER)
                {
                    int luaFunction = lua_tonumber(luaVM, -1);

                    callable = CCallable(resource, luaFunction);
                }
                else
                {
                    lua_pop(luaVM, 1);

                    debugger->LogCustom(luaVM, "Invalid callable reference!");

                    return 0;
                }

                lua_pop(luaVM, 1);

                CLuaArguments arguments;
                CLuaArguments results;

                CScriptArgReader argStream(luaVM);
                argStream.Skip(1);

                argStream.ReadLuaArguments(arguments);

                SString errorMessage;

                if (!callable.Call(&arguments, &results, &errorMessage))
                {
                    debugger->LogCustom(luaVM, errorMessage.empty() ? "Error while calling to callable!" : errorMessage.c_str());
                }

                arguments.DeleteArguments();

                int returnCount = results.Count();

                if (returnCount > 0)
                {
                    results.PushArguments(luaVM);

                    results.DeleteArguments();
                }

                return returnCount;
            }
            else
            {
                debugger->LogCustom(luaVM, "Invalid resource to call!");
            }
        }
        else
        {
            debugger->LogCustom(luaVM, "Couldn't find the resource of callable!");
        }
    }

    return 0;
}

int CLuaArgument::CleanupFunction(lua_State* luaVM)
{
    if (lua_type(luaVM, 1) == LUA_TTABLE)
    {
        lua_getfield(luaVM, 1, "resource");
        CResource* resource = nullptr;
        if (lua_type(luaVM, -1) == LUA_TLIGHTUSERDATA)
        {
            resource = lua_toresource(luaVM, -1);
        }
        lua_pop(luaVM, 1);
        if (resource)
        {
            const char* resourceName = resource->GetName().c_str();
            if (resource->IsActive())
            {
                lua_getfield(luaVM, 1, "reference");
                int reference = 0;
                if (lua_type(luaVM, -1) == LUA_TNUMBER)
                {
                    reference = (int)lua_tonumber(luaVM, -1);
                }
                lua_pop(luaVM, 1);
                lua_pushnil(luaVM);
                lua_setfield(luaVM, -2, "reference");
                if (reference != 0)
                {
                    lua_State* resourceVM = resource->GetVirtualMachine()->GetVM();
                    if (resourceVM)
                    {
                        lua_getref(resourceVM, reference);
                        if (lua_type(resourceVM, -1) != LUA_TNIL)
                        {
                            lua_pop(resourceVM, 1);

                            lua_unref(resourceVM, reference);

                            lua_pushboolean(luaVM, true);
                            return 1;
                        }
                        lua_pop(resourceVM, 1);
                    }
                }
            }
        }
    }
    lua_pushboolean(luaVM, false);
    return 1;
}
