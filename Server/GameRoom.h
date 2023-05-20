#pragma once

#include "Core/Time/Clock.h"

#include "Network/TCPAcceptor.h"
#include "Network/TCPConnector.h"

#include "Synch/Package.h"
#include "Synch/Action.h"

#include "GameActions/GameActions.h"
#include "GameActions/GameActionConfig.h"

#include "GameMgr.h"

struct Client
{
	TCPConnector			conn;
	PackageReceiver			receiver;
	bool					matched = false;
};

class GameRoom
{
public:
	constexpr static size_t MAX_CLIENTS					= 10;

	constexpr static size_t NUM_TICKS_PER_SEND			= GameActionConfig::ServerConfig::NUM_TICKS_PER_SYNCH;

	constexpr static size_t NUM_BUFFERED_TICKS			= GameActionConfig::ServerConfig::NUM_TICKS_PER_SYNCH * 5;

	constexpr static size_t NUM_STREAMS					= NUM_TICKS_PER_SEND + NUM_BUFFERED_TICKS;

	constexpr static size_t NUM_BUFFERED_SEND_PKG		= 32;

	size_t					m_id						= INVALID_ID;
	size_t					m_iterationCount			= 0;
	size_t					m_lastSynchIteration		= 0;

	Client					m_clients[MAX_CLIENTS]		= {};
	size_t					m_clientsCount				= 0;

	PackageSender			m_sender;
	ByteStream				m_sendPkg					;
	//Spinlock				m_sendLocks					[MAX_CLIENTS];

	ByteStream				m_iterationStreams			[NUM_STREAMS];
	uint32_t				m_actionsCount				[NUM_STREAMS];
	size_t					m_actionsCountPkgIdx		[NUM_STREAMS];
	size_t					m_iterationStreamStartIdx	= 0;

	ConcurrentQueue<Action*>	m_internalActions;

	GameMgr					m_gameMgr;

	size_t					m_matchedClient				= 0;
	bool					m_isInitOnce				= false;
	bool					m_isMatchedSuccess			= false;

	inline void FirstResetIterationStreams()
	{
		auto iter = m_iterationCount;
		size_t i = 0;
		for (auto& stream : m_iterationStreams)
		{
			stream.Clean();
			stream.Put<size_t>(++iter);
			m_actionsCountPkgIdx[i] = stream.Put<uint32_t>(0);
			m_actionsCount[i] = 0;
			i++;

			//std::cout << "Prepare stream " << iter << " \n";
		}

		m_iterationStreamStartIdx = 0;
		m_lastSynchIteration = m_iterationCount + 1;
	}

	inline void ResetIterationStreams()
	{
		auto iter = m_iterationCount + NUM_BUFFERED_TICKS;
		for (size_t j = 0; j < NUM_TICKS_PER_SEND; j++)
		{
			auto idx = j + m_iterationStreamStartIdx;
			auto& stream = m_iterationStreams[idx];
			stream.Clean();
			stream.Put<size_t>(++iter);
			m_actionsCountPkgIdx[idx] = stream.Put<uint32_t>(0);
			m_actionsCount[idx] = 0;

			//std::cout << "Prepare stream " << iter << " \n";
		}

		m_iterationStreamStartIdx = (m_iterationStreamStartIdx + NUM_TICKS_PER_SEND) % NUM_STREAMS;
		m_lastSynchIteration = m_iterationCount + 1;
	}

	inline void SynchAllClients()
	{
		m_sendPkg.Clean();

		for (size_t i = 0; i < NUM_TICKS_PER_SEND; i++)
		{
			auto idx = i + m_iterationStreamStartIdx;
			m_iterationStreams[idx].Set<uint32_t>(m_actionsCountPkgIdx[idx], m_actionsCount[idx]);
			//std::cout << "Tick " << i << ", num actions: " << actionsCount[i] << '\n';
		}

		for (size_t i = 0; i < NUM_TICKS_PER_SEND; i++)
		{
			m_sendPkg.Merge(m_iterationStreams[i + m_iterationStreamStartIdx]);
		}
		/*for (auto& stream : iterationStreams)
		{
			sendPkg.Merge(stream);
		}*/

		for (auto& client : m_clients)
		{
			if (!client.conn.IsDisconnected())
			{
				m_sender.SendSynch(m_sendPkg, client.conn);
			}
		}

		ResetIterationStreams();
	}

	// check if user send a valid action
	inline bool IsActionValid(ID userId, Action* action)
	{
		if (action->GetActionID() == GameActions::ACTION_ID::MATCH_START)
		{
			action->Activate(nullptr);
		}

		return true;
	}

	inline void ProcessClientStream(ByteStreamRead& stream)
	{
		size_t minIteration = 0;

		while (!stream.IsEmpty())
		{
			auto clientIteration = stream.Get<size_t>();
			auto actionCount = stream.Get<uint32_t>();

			if (minIteration == 0)
			{
				minIteration = clientIteration;
			}

#ifdef _DEBUG
			if (minIteration <= clientIteration)
			{
				int x = 3;
				assert(minIteration <= clientIteration);
			}

			if (clientIteration <= m_iterationCount)
			{
				int x = 3;
				assert(clientIteration <= m_iterationCount);
			}
#endif // _DEBUG

			auto offsetIteration = clientIteration - minIteration;

			if (offsetIteration >= NUM_BUFFERED_TICKS)
			{
				std::cout << offsetIteration << '\n';
			}

			//auto serverActivateIteration = lastSynchIteration + offsetIteration;

			auto streamIdx = (m_iterationStreamStartIdx + offsetIteration) % NUM_STREAMS;

			auto& sendStream = m_iterationStreams[streamIdx];
			uint32_t& validAction = m_actionsCount[streamIdx];

			//std::cout << "Recv " << actionCount << " actions\n";

			for (size_t i = 0; i < actionCount; i++)
			{
				ActionID aId = stream.Get<ActionID>();
				auto action = ActionCreator::New(aId);
				action->Deserialize(stream);

				if (IsActionValid(i, action))
				{
					sendStream.Put<ActionID>(action->GetActionID());
					action->Serialize(sendStream);
					validAction++;
				}

				ActionCreator::Delete(action);
			}
		}
	}

	inline void ProcessAllClients()
	{
		ConsumeActions();

		size_t disconnectedClient = 0;
		for (auto& client : m_clients)
		{
			if (!client.conn.IsDisconnected())
			{
				auto ercode = client.receiver.RecvSynch(client.conn);
				if (ercode == 0)
				{
					// success

					auto clientStream = client.receiver.GetStream();
					ProcessClientStream(clientStream);
				}
				else if (ercode == PackageReceiver::ERCODE::CONNECTION_ERROR)
				{
					client.conn.Disconnect();
				}
			}
			else
			{
				disconnectedClient++;
			}
		}

		if (disconnectedClient == m_clientsCount)
		{
			Abort();
		}
	}

	inline void ConsumeActions()
	{
		Action* action;
		auto offsetIteration = m_iterationCount - m_lastSynchIteration;
		auto streamIdx = (m_iterationStreamStartIdx + offsetIteration) % NUM_STREAMS;
		auto& sendStream = m_iterationStreams[streamIdx];
		uint32_t& validAction = m_actionsCount[streamIdx];

		while (true)
		{
			if (!m_internalActions.try_dequeue(action))
			{
				break;
			}

			sendStream.Put<ActionID>(action->GetActionID());
			action->Serialize(sendStream);
			validAction++;

			ActionCreator::Delete(action);
		}
	}

	inline void FixedIteration(float dt)
	{
		if (!m_isMatchedSuccess)
		{
			ProcessAllClients();
			return;
		}

		m_iterationCount++;

		if (m_iterationCount % NUM_TICKS_PER_SEND == 0)
		{
			SynchAllClients();
		}

		Update(dt);
		ProcessAllClients();
	}

	inline void SendMatchStartAction()
	{
		MatchStartAction* matchStart = (MatchStartAction*)ActionCreator::New(GameActions::ACTION_ID::MATCH_START);

		matchStart->m_numClient = m_clientsCount;
		matchStart->m_roomID = m_id;

		m_gameMgr.GenerateMap(
			matchStart->m_map, matchStart->m_width, matchStart->m_height,
			matchStart->m_blockCellValues, matchStart->m_numBlockCell);

		for (size_t i = 0; i < m_clientsCount; i++)
		{
			auto& clientInfo = matchStart->m_clientInfo[i];
			auto& client	 = m_clients[i];

			clientInfo.id = i;
			clientInfo.ipAddr = client.conn.GetPeerAddressString();
			m_gameMgr.SetPlayerPos(i, clientInfo.pos);

			std::cout << "Send start for client " << clientInfo.ipAddr << "\n";
		}

		auto& stream = m_iterationStreams[0];
		stream.Clean();
		stream.Put<size_t>(1);
		stream.Put<uint32_t>(1);
		stream.Put<ActionID>(matchStart->GetActionID());
		matchStart->Serialize(stream);

		for (auto& client : m_clients)
		{
			m_sender.SendSynch(stream, client.conn);
		}

		ActionCreator::Delete(matchStart);
	}

	inline void StartUp()
	{
		if (m_isInitOnce == false)
		{
			m_sendPkg.Initialize(Package::MAX_LEN);
			for (auto& stream : m_iterationStreams)
			{
				stream.Initialize(64 * KB);
			}
			m_isInitOnce = true;
		}
		else
		{
			m_sendPkg.Clean();
			for (auto& stream : m_iterationStreams)
			{
				stream.Clean();
			}
		}
		
		SendMatchStartAction();
		FirstResetIterationStreams();
		m_iterationCount++;
	}

	inline void Abort()
	{
		m_id = INVALID_ID;
		m_iterationCount = 0;
		m_clientsCount = 0;
		m_iterationStreamStartIdx = 0;
		m_matchedClient = 0;
		m_isMatchedSuccess = false;
		
		for (auto& client : m_clients)
		{
			client.matched = false;
		}
	}

	inline auto& GetClientConn(size_t id)
	{
		return m_clients[id].conn;
	}

	inline void InitializeClient(size_t id)
	{
		m_clients[id].receiver.Initialize();
	}

	// thread-safe method
	inline void ExecuteAction(Action* action)
	{
		m_internalActions.enqueue(action);
	}

	inline void OnClientMatchedSuccess(ID clientId)
	{
		if (m_clients[clientId].matched == false)
		{
			m_clients[clientId].matched == true;
			m_matchedClient++;
		}
		
		if (m_matchedClient == m_clientsCount)
		{
			m_isMatchedSuccess = true;
		}
	}

public:
	inline virtual void Update(float dt) {};

	static GameRoom* GetRoom(size_t id);
};