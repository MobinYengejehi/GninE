/*****************************************************************************
 *
 *  PROJECT:     GninE v1.0
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        mods/deathmatch/logic/lua/CLuaArguments.h
 *  PURPOSE:     Lua argument manager class
 *
 *  GninE is available from http://www.gnine.com/
 *
 *****************************************************************************/

#pragma once

extern "C"
{
    #include "lua.h"
}

#include "CLuaArgument.h"
#include <vector>
#include "../common/CBitStream.h"
#include "json.h"
#include "CLuaFunctionRef.h"

#include "wasm/WebAssemblyImports.h"

#define C_ARGS_SEPRATOR ",^"
#define C_ARGS_SEPRATOR_LENGTH (sizeof(C_ARGS_SEPRATOR) - 1)
#define C_ARGS_TYPE_IDENTIFIER_LENGTH (sizeof(char))
#define C_ARGS_STRING_SYNTAX "\""
#define C_ARGS_STRING_SYNTAX_LENGTH (sizeof(C_ARGS_STRING_SYNTAX) - 1)
#define C_ARGS_STRING_PASS_SYNTAX "\\"
#define C_ARGS_STRING_PASS_SYNTAX_LENGTH (sizeof(C_ARGS_STRING_PASS_SYNTAX) - 1)
#define C_ARGS_LIST_OPEN_SYNTAX "{"
#define C_ARGS_LIST_OPEN_SYNTAX_LENGTH (sizeof(C_ARGS_LIST_OPEN_SYNTAX) - 1)
#define C_ARGS_LIST_CLOSE_SYNTAX "}"
#define C_ARGS_LIST_CLOSE_SYNTAX_LENGTH (sizeof(C_ARGS_LIST_CLOSE_SYNTAX) - 1)
#define C_ARGS_LIST_ARGC_OPEN_SYNTAX "("
#define C_ARGS_LIST_ARGC_OPEN_SYNTAX_LENGTH (sizeof(C_ARGS_LIST_ARGC_OPEN_SYNTAX) - 1)
#define C_ARGS_LIST_ARGC_CLOSE_SYNTAX ")"
#define C_ARGS_LIST_ARGC_CLOSE_SYNTAX_LENGTH (sizeof(C_ARGS_LIST_ARGC_CLOSE_SYNTAX) - 1)
#define C_ARGS_INVALID_POSITION ((long long)-1)

enum C_ARGS_TYPE_IDENTIFIERS : char {
    C_ARGS_TYPE_ID_UNKNOWN = 0,
    C_ARGS_TYPE_ID_NULL = '0',
    C_ARGS_TYPE_ID_BOOLEAN = 'b',
    C_ARGS_TYPE_ID_NUMBER = 'n',
    C_ARGS_TYPE_ID_STRING = 's',
    C_ARGS_TYPE_ID_CALLABLE = 'c',
    C_ARGS_TYPE_ID_USERDATA = 'u',
    C_ARGS_TYPE_ID_LIST = 'l'
};

inline void LUA_CHECKSTACK(lua_State* L, int size)
{
    if (lua_getstackgap(L) < size + 5)
        lua_checkstack(L, (size + 2) * 3 + 4);
}

class CAccessControlList;
class CAccessControlListGroup;
class CAccount;
class CBan;
class CElement;
class CLuaTimer;
class CResource;
class CTextDisplay;
class CTextItem;
class CDbJobData;

class CWebAssemblyVariables;
class CWebAssemblyMemory;

class CLuaArguments;

class CLuaArguments
{
public:
    CLuaArguments() {}
    CLuaArguments(const CLuaArguments& Arguments, CFastHashMap<CLuaArguments*, CLuaArguments*>* pKnownTables = NULL);

    ~CLuaArguments() { DeleteArguments(); };

    void CopyRecursive(const CLuaArguments& Arguments, CFastHashMap<CLuaArguments*, CLuaArguments*>* pKnownTables = NULL);

    const CLuaArguments& operator=(const CLuaArguments& Arguments);
    CLuaArgument*        operator[](const unsigned int uiPosition) const;

    void ReadArgument(lua_State* luaVM, signed int uiIndex);
    void ReadArguments(lua_State* luaVM, signed int uiIndexBegin = 1);
    void PushArguments(lua_State* luaVM) const;
    void PushArguments(const CLuaArguments& Arguments);
    bool Call(class CLuaMain* pLuaMain, const CLuaFunctionRef& iLuaFunction, CLuaArguments* returnValues = NULL) const;
    bool CallGlobal(class CLuaMain* pLuaMain, const char* szFunction, CLuaArguments* returnValues = NULL) const;

    void ReadTable(lua_State* luaVM, int iIndexBegin, CFastHashMap<const void*, CLuaArguments*>* pKnownTables = NULL);
    void PushAsTable(lua_State* luaVM, CFastHashMap<CLuaArguments*, int>* pKnownTables = nullptr) const;

    CLuaArgument* PushNil();
    CLuaArgument* PushBoolean(bool bBool);
    CLuaArgument* PushNumber(double dNumber);
    CLuaArgument* PushString(const std::string& strString);
    CLuaArgument* PushElement(CElement* pElement);
    CLuaArgument* PushBan(CBan* pBan);
    CLuaArgument* PushACL(CAccessControlList* pACL);
    CLuaArgument* PushACLGroup(CAccessControlListGroup* pACLGroup);
    CLuaArgument* PushAccount(CAccount* pAccount);
    CLuaArgument* PushResource(CResource* pResource);
    CLuaArgument* PushTextDisplay(CTextDisplay* pTextDisplay);
    CLuaArgument* PushTextItem(CTextItem* pTextItem);
    CLuaArgument* PushTimer(CLuaTimer* pLuaTimer);
    CLuaArgument* PushDbQuery(CDbJobData* pJobData);

    CLuaArgument* PushArgument(const CLuaArgument& argument);
    CLuaArgument* PushTable(CLuaArguments* table);

    void DeleteArguments();
    void ValidateTableKeys();
    void Pop();

    bool         ReadFromBitStream(NetBitStreamInterface& bitStream, std::vector<CLuaArguments*>* pKnownTables = NULL);
    bool         ReadFromJSONString(const char* szJSON);
    bool         WriteToBitStream(NetBitStreamInterface& bitStream, CFastHashMap<CLuaArguments*, unsigned long>* pKnownTables = NULL) const;
    bool         WriteToJSONString(std::string& strJSON, bool bSerialize = false, int flags = JSON_C_TO_STRING_PLAIN);
    json_object* WriteTableToJSONObject(bool bSerialize = false, CFastHashMap<CLuaArguments*, unsigned long>* pKnownTables = NULL);
    json_object* WriteToJSONArray(bool bSerialize);
    bool         ReadFromJSONObject(json_object* object, std::vector<CLuaArguments*>* pKnownTables = NULL);
    bool         ReadFromJSONArray(json_object* object, std::vector<CLuaArguments*>* pKnownTables = NULL);

    void WriteWebAssemblyVariables(CWebAssemblyVariables* vars, CWebAssemblyVariables* typePattern = NULL, CWebAssemblyMemory* wasmMemory = NULL, std::vector<CWebAssemblyMemoryPointerAddress>* pointersToDelete = NULL);
    void ReadWebAssemblyVariables(CWebAssemblyVariables* vars);
    void UnpackCompiledArguments(uint8_t* data, size_t size, bool readAsTable = false);

    unsigned int                               Count() const { return static_cast<unsigned int>(m_Arguments.size()); };
    std::vector<CLuaArgument*>::const_iterator IterBegin() const { return m_Arguments.begin(); };
    std::vector<CLuaArgument*>::const_iterator IterEnd() const { return m_Arguments.end(); };

    bool IsEqualTo(const CLuaArguments& compareTo, std::set<const CLuaArguments*>* knownTables = nullptr) const;

private:
    std::vector<CLuaArgument*> m_Arguments;
};
