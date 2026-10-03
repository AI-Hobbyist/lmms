if(WANT_AGENT_MCP)
	list(APPEND LMMS_SRCS agent/mcp/HttpMcpServer.cpp)
endif()

set(LMMS_SRCS
	${LMMS_SRCS}

	agent/CommandBus.cpp
	agent/CommandSchema.cpp
	agent/CoreCommands.cpp
	agent/ExportCommands.cpp
	agent/ToolRegistry.cpp
	agent/ScriptExpression.cpp
	agent/ProjectSnapshot.cpp
	agent/ScriptRunner.cpp
	agent/HighLevelCommands.cpp

	PARENT_SCOPE
)
