if(WANT_AGENT_MCP)
	list(APPEND LMMS_SRCS agent/mcp/HttpMcpServer.cpp agent/mcp/McpProtocol.cpp)
endif()

if(WANT_AGENT_CHAT)
	list(APPEND LMMS_SRCS agent/chat/ChatStore.cpp agent/chat/ChatDock.cpp agent/chat/ChatDockLayout.cpp
		agent/chat/ChatSettingsDialog.cpp agent/chat/ChatSessionsDialog.cpp agent/chat/ChatConnection.cpp
		agent/chat/ChatEventStream.cpp agent/chat/ChatApiSettingsPage.cpp agent/chat/ChatAdvancedDialog.cpp
		agent/chat/ChatGenerationPage.cpp agent/chat/ChatRequest.cpp agent/chat/ChatResponse.cpp
		agent/chat/ChatProviderClient.cpp agent/chat/ChatController.cpp agent/chat/ChatDockRequest.cpp
		agent/chat/ChatAttachment.cpp agent/chat/ChatContent.cpp agent/chat/ChatAttachmentsDialog.cpp
		agent/chat/ChatTranscript.cpp agent/chat/ChatTools.cpp agent/chat/ChatToolDispatcher.cpp
		agent/chat/ChatSearchSettings.cpp agent/chat/ChatWebSearch.cpp
		agent/chat/ChatPortableTools.cpp)
endif()

set(LMMS_SRCS
	${LMMS_SRCS}
	agent/CommandBus.cpp
	agent/CommandSchema.cpp
	agent/CommandTargets.cpp
	agent/CoreCommands.cpp
	agent/DiscoveryCommands.cpp
	agent/MixerCommands.cpp
	agent/SvsCommands.cpp
	agent/SvsPitchCommands.cpp
	agent/SvcCommands.cpp
	agent/ExportCommands.cpp
	agent/ToolRegistry.cpp
	agent/ScriptExpression.cpp
	agent/ProjectSnapshot.cpp
	agent/ScriptRunner.cpp
	agent/HighLevelCommands.cpp
	PARENT_SCOPE)
