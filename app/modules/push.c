#include "push.h"
#include "lualib.h"
#include "lauxlib.h"
#include "auxmods.h"
#include "lrotable.h"

#include <c_types.h>
#include <osapi.h>

/*
 * push.regist(appid, appkey, function(msg));
 * push.unregist();
 * push.get_status();
 * push.pushmsg(msg);
 *
 */


static int push_data_recved = LUA_NOREF;
static lua_State* gL = NULL;
static lua_State* gL_rtstatus = NULL;


void msg_recv(uint8* pdata, uint32 len)
{
	if(!gL) {
		uart0_sendStr("pls regist first.\r\n");
		return;
	}

	if(push_data_recved == LUA_NOREF) {
		return;
	}

	lua_rawgeti(gL, LUA_REGISTRYINDEX, push_data_recved);

	lua_pushlstring(gL, (const char*)pdata, len);
	lua_call(gL, 1, 0);
}


/*
 * 实时状态获取的回调函数
 */
void rtstatus_cb_func(uint32 msgid, char* key, int16_t length)
{
	if(!gL_rtstatus) {
		uart0_sendStr("pls regist first.\r\n");
		return;
	}

	lua_pushstring(gL_rtstatus, key);
	lua_gettable(gL_rtstatus, LUA_REGISTRYINDEX);
	lua_call(gL_rtstatus, 0, 1);
	const char* result = lua_tostring(gL_rtstatus, -1);

	espush_rtstatus_ret_to_gateway(msgid, result, lua_strlen(gL_rtstatus, -1));
}


void remote_luafile_cb(uint8* filebuf, uint32 len)
{
//	uart0_sendStr(filebuf);
	if(!gL) {
		uart0_sendStr("pls regist first.\r\n");
		return;
	}

	int iRet = luaL_loadbuffer(gL, (const char*)filebuf, len, "REMOTE_LUAFILE_CMD");
	if(iRet == LUA_ERRSYNTAX) {
		uart0_sendStr("COMPILE ERROR\r\n");
		return;
	} else if(iRet == LUA_ERRMEM) {
		uart0_sendStr("ERROR MEMORY\r\n");
		return;
	}

	iRet = lua_pcall(gL, 0, 0, 0);
	if(iRet) {
		char iRetBuf[64] = { 0 };
		os_sprintf(iRetBuf, "ERROR:[%s]\r\n", iRet, lua_tostring(gL, -1));
		uart0_sendStr(iRetBuf);
		lua_pop(gL, 1);
		return;
	}
}


static int regist(lua_State* L)
{
	uint32 appid = luaL_checkinteger(L, 1);
	const char* appkey = luaL_checkstring(L, 2);
	if(!appkey || os_strlen(appkey) != 32) {
		return luaL_error(L, "appkey arguments error");
	}

	if(espush_server_connect_status() == STATUS_CONNECTED) {
		return luaL_error(L, "connected.");
	}

	gL = L;
	if (lua_type(L, 3) == LUA_TFUNCTION || lua_type(L, 3) == LUA_TLIGHTFUNCTION) {
		lua_pushvalue(L, 3);
		if(push_data_recved != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, push_data_recved);
		}

		push_data_recved = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	//(uint32 appid, char appkey[32], char devid[32], enum VERTYPE type, msg_cb msgcb);
	espush_register(appid, (char*)appkey, "NODEMCU_ANONYMOUS", VER_NODEMCU, msg_recv);
	espush_luafile_cb(remote_luafile_cb);
	espush_rtstatus_cb(rtstatus_cb_func);

	lua_pushinteger(L, 0);
	return 1;
}


static int unregist(lua_State* L)
{
	espush_unregister();
	luaL_unref(L, LUA_REGISTRYINDEX, push_data_recved);
	gL = NULL;
	push_data_recved = LUA_NOREF;
	return 0;
}


static int get_status(lua_State* L)
{
	lua_pushinteger(L, espush_server_connect_status());
	return 1;
}


static int pushmsg(lua_State* L)
{
	size_t msg_length = 0;
	const char* msg = luaL_checklstring(L, 1, &msg_length);
	sint8 iRet = espush_msg((uint8*)msg, msg_length);

	lua_pushinteger(L, iRet);
	return 1;
}


static int set_status_flag(lua_State* L)
{
	const char* flag_key = luaL_checkstring(L, 1);
	if(!flag_key || os_strlen(flag_key) <= 0) {
		uart0_sendStr("KEY ERROR");
		return luaL_error(L, "appkey arguments error");
	}
	gL_rtstatus = L;
	if (lua_type(L, 2) == LUA_TFUNCTION || lua_type(L, 2) == LUA_TLIGHTFUNCTION) {
		lua_pushstring(L, flag_key);
		lua_pushvalue(L, 2);
		lua_settable(L, LUA_REGISTRYINDEX);
	}

	return 0;
}


#define MIN_OPT_LEVEL 2
#include "lrodefs.h"


const LUA_REG_TYPE push_map[] = {
	{LSTRKEY("regist"), LFUNCVAL(regist)},
	{LSTRKEY("unregist"), LFUNCVAL(unregist)},
	{LSTRKEY("get_status"), LFUNCVAL(get_status)},
	{LSTRKEY("pushmsg"), LFUNCVAL(pushmsg)},
	{LSTRKEY("set_status_flag"), LFUNCVAL(set_status_flag)},

	{LSTRKEY("CONNECTING"), LNUMVAL(STATUS_CONNECTING)},
	{LSTRKEY("DNS_LOOKUP"), LNUMVAL(STATUS_DNS_LOOKUP)},
	{LSTRKEY("CONNECTED"), LNUMVAL(STATUS_CONNECTED)},
	{LSTRKEY("DISCONNECTED"), LNUMVAL(STATUS_DISCONNECTED)},

	{LSTRKEY("__metatable"), LROVAL(push_map)},
	{ LNILKEY, LNILVAL }
};


LUALIB_API int luaopen_push( lua_State *L )
{
	return 0;
}

/*
 * TODO:
 * [√] 加入常量
 * [√] 加入错误判断，加入重复regist的判断
 */
