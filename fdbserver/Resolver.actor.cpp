/*
 * Resolver.actor.cpp
 *
 * This source file is part of the FoundationDB open source project
 *
 * Copyright 2013-2018 Apple Inc. and the FoundationDB project authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fdbclient/FDBTypes.h"
#include "fdbclient/NativeAPI.actor.h"
#include "fdbclient/Notified.h"
#include "fdbclient/StorageServerInterface.h"
#include "fdbclient/SystemData.h"
#include "fdbserver/ApplyMetadataMutation.h"
#include "fdbserver/ConflictSet.h"
#include "fdbserver/IKeyValueStore.h"
#include "fdbserver/Knobs.h"
#include "fdbserver/LogSystem.h"
#include "fdbserver/LogSystemDiskQueueAdapter.h"
#include "fdbserver/MasterInterface.h"
#include "fdbserver/ptxn/TLogGroupVersionTracker.h"
#include "fdbserver/ResolverInterface.h"
#include "fdbserver/RestoreUtil.h"
#include "fdbserver/ServerDBInfo.h"
#include "fdbserver/StorageMetrics.h"
#include "fdbserver/WaitFailure.h"
#include "fdbserver/WorkerInterface.actor.h"
#include "flow/ActorCollection.h"
#include "flow/Error.h"

#include "flow/actorcompiler.h" // This must be the last #include.

namespace {
struct ProxyRequestsInfo {
	std::map<Version, ResolveTransactionBatchReply> outstandingBatches;
	Version lastVersion = invalidVersion;

	ProxyRequestsInfo() = default;
};

struct Resolver : ReferenceCounted<Resolver> {
	const UID dbgid;
	const int commitProxyCount, resolverCount;
	NotifiedVersion version;
	AsyncVar<Version> neededVersion;
	ptxn::TLogGroupVersionTracker versionTracker; // tracks per group commit version

	Map<Version, Standalone<VectorRef<StateTransactionRef>>> recentStateTransactions;
	Deque<std::pair<Version, int64_t>> recentStateTransactionSizes;
	AsyncVar<int64_t> totalStateBytes;
	AsyncTrigger checkNeededVersion;
	std::map<NetworkAddress, ProxyRequestsInfo> proxyInfoMap;
	ConflictSet* conflictSet;
	TransientStorageMetricSample iopsSample;

	// Use LogSystem as backend for txnStateStore. However, the real commit
	// happens at commit proxies and we never "write" to the LogSystem at
	// Resolvers.
	LogSystemDiskQueueAdapter* logAdapter = nullptr;
	Reference<ILogSystem> logSystem;
	IKeyValueStore* txnStateStore = nullptr;

	std::map<UID, Reference<StorageInfo>> storageCache;
	KeyRangeMap<ServerCacheInfo> keyInfo; // keyrange -> all storage servers in all DCs for the keyrange
	std::unordered_map<UID, StorageServerInterface> tssMapping;
	bool forceRecovery = false;

	Reference<TLogGroupCollection> tLogGroupCollection;

	Version debugMinRecentStateVersion = 0;

	CounterCollection cc;
	Counter resolveBatchIn;
	Counter resolveBatchStart;
	Counter resolvedTransactions;
	Counter resolvedBytes;
	Counter resolvedReadConflictRanges;
	Counter resolvedWriteConflictRanges;
	Counter transactionsAccepted;
	Counter transactionsTooOld;
	Counter transactionsConflicted;
	Counter resolvedStateTransactions;
	Counter resolvedStateMutations;
	Counter resolvedStateBytes;
	Counter resolveBatchOut;
	Counter metricsRequests;
	Counter splitRequests;

	Future<Void> logger;

	Resolver(UID dbgid, int commitProxyCount, int resolverCount)
	  : dbgid(dbgid), commitProxyCount(commitProxyCount), resolverCount(resolverCount), version(-1),
	    conflictSet(newConflictSet()), iopsSample(SERVER_KNOBS->KEY_BYTES_PER_SAMPLE), cc("Resolver", dbgid.toString()),
	    resolveBatchIn("ResolveBatchIn", cc), resolveBatchStart("ResolveBatchStart", cc),
	    resolvedTransactions("ResolvedTransactions", cc), resolvedBytes("ResolvedBytes", cc),
	    resolvedReadConflictRanges("ResolvedReadConflictRanges", cc),
	    resolvedWriteConflictRanges("ResolvedWriteConflictRanges", cc),
	    transactionsAccepted("TransactionsAccepted", cc), transactionsTooOld("TransactionsTooOld", cc),
	    transactionsConflicted("TransactionsConflicted", cc),
	    resolvedStateTransactions("ResolvedStateTransactions", cc),
	    resolvedStateMutations("ResolvedStateMutations", cc), resolvedStateBytes("ResolvedStateBytes", cc),
	    resolveBatchOut("ResolveBatchOut", cc), metricsRequests("MetricsRequests", cc),
	    splitRequests("SplitRequests", cc) {
		specialCounter(cc, "Version", [this]() { return this->version.get(); });
		specialCounter(cc, "NeededVersion", [this]() { return this->neededVersion.get(); });
		specialCounter(cc, "TotalStateBytes", [this]() { return this->totalStateBytes.get(); });

		logger = traceCounters("ResolverMetrics", dbgid, SERVER_KNOBS->WORKER_LOGGING_INTERVAL, &cc, "ResolverMetrics");
	}
	~Resolver() { destroyConflictSet(conflictSet); }
};
} // namespace

ACTOR Future<Void> resolveBatch(Reference<Resolver> self, ResolveTransactionBatchRequest req) {
	state Optional<UID> debugID;
	state Span span("R:resolveBatch"_loc, req.spanContext);

	// The first request (prevVersion < 0) comes from the master
	state NetworkAddress proxyAddress =
	    req.prevVersion >= 0 ? req.reply.getEndpoint().getPrimaryAddress() : NetworkAddress();
	state ProxyRequestsInfo& proxyInfo = self->proxyInfoMap[proxyAddress];

	self->versionTracker.addGroups(req.newGroups, req.version);
	self->versionTracker.removeGroups(req.staleGroups);

	++self->resolveBatchIn;

	if (req.debugID.present()) {
		debugID = nondeterministicRandom()->randomUniqueID();
		g_traceBatch.addAttach("CommitAttachID", req.debugID.get().first(), debugID.get().first());
		g_traceBatch.addEvent("CommitDebug", debugID.get().first(), "Resolver.resolveBatch.Before");
	}

	/*TraceEvent("ResolveBatchStart", self->dbgid).detail("From", proxyAddress).detail("Version", req.version).detail("PrevVersion", req.prevVersion).detail("StateTransactions", req.txnStateTransactions.size())
	    .detail("RecentStateTransactions", self->recentStateTransactionSizes.size()).detail("LastVersion",
	   proxyInfo.lastVersion).detail("FirstVersion", self->recentStateTransactionSizes.empty() ? -1 :
	   self->recentStateTransactionSizes.front().first) .detail("ResolverVersion", self->version.get());*/

	while (self->totalStateBytes.get() > SERVER_KNOBS->RESOLVER_STATE_MEMORY_LIMIT &&
	       self->recentStateTransactionSizes.size() &&
	       proxyInfo.lastVersion > self->recentStateTransactionSizes.front().first &&
	       req.version > self->neededVersion.get()) {
		/*TraceEvent("ResolveBatchDelay").detail("From", proxyAddress).detail("StateBytes", self->totalStateBytes.get()).detail("RecentStateTransactionSize", self->recentStateTransactionSizes.size())
		    .detail("LastVersion", proxyInfo.lastVersion).detail("RequestVersion", req.version).detail("NeededVersion",
		   self->neededVersion.get()) .detail("RecentStateVer", self->recentStateTransactions.begin()->key);*/

		wait(self->totalStateBytes.onChange() || self->neededVersion.onChange());
	}

	if (debugID.present()) {
		g_traceBatch.addEvent("CommitDebug", debugID.get().first(), "Resolver.resolveBatch.AfterQueueSizeCheck");
	}

	loop {
		if (self->recentStateTransactionSizes.size() &&
		    proxyInfo.lastVersion <= self->recentStateTransactionSizes.front().first) {
			self->neededVersion.set(std::max(self->neededVersion.get(), req.prevVersion));
		}

		choose {
			when(wait(self->version.whenAtLeast(req.prevVersion))) { break; }
			when(wait(self->checkNeededVersion.onTrigger())) {}
		}
	}

	if (check_yield(TaskPriority::DefaultEndpoint)) {
		wait(delay(0, TaskPriority::Low) || delay(SERVER_KNOBS->COMMIT_SLEEP_TIME)); // FIXME: Is this still right?
		g_network->setCurrentTask(TaskPriority::DefaultEndpoint);
	}

	if (self->version.get() ==
	    req.prevVersion) { // Not a duplicate (check relies on no waiting between here and self->version.set() below!)
		++self->resolveBatchStart;
		self->resolvedTransactions += req.transactions.size();
		self->resolvedBytes += req.transactions.expectedSize();

		if (proxyInfo.lastVersion > 0) {
			proxyInfo.outstandingBatches.erase(proxyInfo.outstandingBatches.begin(),
			                                   proxyInfo.outstandingBatches.upper_bound(req.lastReceivedVersion));
		}

		Version firstUnseenVersion = proxyInfo.lastVersion + 1;
		proxyInfo.lastVersion = req.version;

		if (req.debugID.present())
			g_traceBatch.addEvent("CommitDebug", debugID.get().first(), "Resolver.resolveBatch.AfterOrderer");

		ResolveTransactionBatchReply& reply = proxyInfo.outstandingBatches[req.version];

		std::vector<int> commitList;
		std::vector<int> tooOldList;

		// Detect conflicts
		double expire = now() + SERVER_KNOBS->SAMPLE_EXPIRATION_TIME;
		ConflictBatch conflictBatch(self->conflictSet, &reply.conflictingKeyRangeMap, &reply.arena);
		int keys = 0;
		for (int t = 0; t < req.transactions.size(); t++) {
			conflictBatch.addTransaction(req.transactions[t]);
			self->resolvedReadConflictRanges += req.transactions[t].read_conflict_ranges.size();
			self->resolvedWriteConflictRanges += req.transactions[t].write_conflict_ranges.size();
			keys += req.transactions[t].write_conflict_ranges.size() * 2 +
			        req.transactions[t].read_conflict_ranges.size() * 2;

			if (self->resolverCount > 1) {
				for (auto it : req.transactions[t].write_conflict_ranges)
					self->iopsSample.addAndExpire(
					    it.begin, SERVER_KNOBS->SAMPLE_OFFSET_PER_KEY + it.begin.size(), expire);
				for (auto it : req.transactions[t].read_conflict_ranges)
					self->iopsSample.addAndExpire(
					    it.begin, SERVER_KNOBS->SAMPLE_OFFSET_PER_KEY + it.begin.size(), expire);
			}
		}
		conflictBatch.detectConflicts(
		    req.version, req.version - SERVER_KNOBS->MAX_WRITE_TRANSACTION_LIFE_VERSIONS, commitList, &tooOldList);

		reply.debugID = req.debugID;
		reply.committed.resize(reply.arena, req.transactions.size());
		for (int c = 0; c < commitList.size(); c++)
			reply.committed[commitList[c]] = ConflictBatch::TransactionCommitted;

		for (int c = 0; c < tooOldList.size(); c++) {
			ASSERT(reply.committed[tooOldList[c]] == ConflictBatch::TransactionConflict);
			reply.committed[tooOldList[c]] = ConflictBatch::TransactionTooOld;
		}

		self->transactionsAccepted += commitList.size();
		self->transactionsTooOld += tooOldList.size();
		self->transactionsConflicted += req.transactions.size() - commitList.size() - tooOldList.size();

		ASSERT(req.prevVersion >= 0 ||
		       req.txnStateTransactions.size() == 0); // The master's request should not have any state transactions

		auto& stateTransactions = self->recentStateTransactions[req.version];
		int64_t stateMutations = 0;
		int64_t stateBytes = 0;
		LogPushData toCommit(self->logSystem); // For accumulating private mutations
		std::unordered_map<ptxn::TLogGroupID, std::shared_ptr<ptxn::ProxySubsequencedMessageSerializer>>
		    groupMessageBuilders;
		toCommit.pGroupMessageBuilders = &groupMessageBuilders;
		ResolverData resolverData(self->dbgid,
		                          self->logSystem,
		                          self->txnStateStore,
		                          &self->keyInfo,
		                          &toCommit,
		                          &self->forceRecovery,
		                          req.version + 1,
		                          &self->storageCache,
		                          &self->tssMapping,
		                          req.version,
		                          self->tLogGroupCollection);
		bool isLocked = false;
		if (SERVER_KNOBS->PROXY_USE_RESOLVER_PRIVATE_MUTATIONS) {
			auto lockedKey = self->txnStateStore->readValue(databaseLockedKey).get();
			isLocked = lockedKey.present() && lockedKey.get().size();
		}
		for (int t : req.txnStateTransactions) {
			stateMutations += req.transactions[t].mutations.size();
			stateBytes += req.transactions[t].mutations.expectedSize();
			stateTransactions.push_back_deep(
			    stateTransactions.arena(),
			    StateTransactionRef(reply.committed[t] == ConflictBatch::TransactionCommitted,
			                        req.transactions[t].mutations));

			// for (const auto& m : req.transactions[t].mutations)
			//	DEBUG_MUTATION("Resolver", req.version, m, self->dbgid);

			// Generate private mutations for metadata mutations
			// The condition here must match CommitBatch::applyMetadataToCommittedTransactions()
			if (reply.committed[t] == ConflictBatch::TransactionCommitted && !self->forceRecovery &&
			    SERVER_KNOBS->PROXY_USE_RESOLVER_PRIVATE_MUTATIONS && (!isLocked || req.transactions[t].lock_aware)) {
				applyMetadataMutations(req.transactions[t].spanContext, resolverData, req.transactions[t].mutations);
			}
			TEST(self->forceRecovery); // Resolver detects forced recovery
		}

		// Update group versions
		std::set<ptxn::TLogGroupID> writtenGroups; // TLog groups been written
		if (SERVER_KNOBS->TLOG_NEW_INTERFACE && req.prevVersion >= 0) { // Not first request
			writtenGroups.insert(req.updatedGroups.begin(), req.updatedGroups.end());
			auto& more = toCommit.getWrittenTLogGroups();
			writtenGroups.insert(more.begin(), more.end());
			// TODO(jingyu): shardChange could happen in the txnStateTransactions from
			// earlier versions, so need to take those into account--scanning through
			// any previous versions that are sent back in txnStateTransactions.
			reply.previousCommitVersions = self->versionTracker.updateGroups(
			    writtenGroups, req.version, toCommit.isShardChanged() ? UpdateAllGroups::True : UpdateAllGroups::False);
		}

		// Adds private mutation messages to the reply message.
		if (SERVER_KNOBS->PROXY_USE_RESOLVER_PRIVATE_MUTATIONS) {
			if (SERVER_KNOBS->TLOG_NEW_INTERFACE) {
				auto groupPrivateMutations = toCommit.getGroupMutations(writtenGroups);
				for (const auto& [group, teamData] : groupPrivateMutations) {
					reply.groupPrivateMutations.emplace(group, teamData.second);
					reply.arena.dependsOn(teamData.first);
				}
			} else {
				auto privateMutations = toCommit.getAllMessages();
				for (const auto& mutations : privateMutations) {
					reply.privateMutations.push_back(reply.arena, mutations);
					reply.arena.dependsOn(mutations.arena());
				}
				reply.privateMutationCount = toCommit.getMutationCount();
			}
		}

		self->resolvedStateTransactions += req.txnStateTransactions.size();
		self->resolvedStateMutations += stateMutations;
		self->resolvedStateBytes += stateBytes;

		if (stateBytes > 0)
			self->recentStateTransactionSizes.emplace_back(req.version, stateBytes);

		ASSERT(req.version >= firstUnseenVersion);
		ASSERT(firstUnseenVersion >= self->debugMinRecentStateVersion);

		TEST(firstUnseenVersion == req.version); // Resolver first unseen version is current version

		auto stateTransactionItr = self->recentStateTransactions.lower_bound(firstUnseenVersion);
		auto endItr = self->recentStateTransactions.lower_bound(req.version);
		for (; stateTransactionItr != endItr; ++stateTransactionItr) {
			reply.stateMutations.push_back(reply.arena, stateTransactionItr->value);
			reply.arena.dependsOn(stateTransactionItr->value.arena());
		}

		//TraceEvent("ResolveBatch", self->dbgid).detail("PrevVersion", req.prevVersion).detail("Version", req.version).detail("StateTransactionVersions", self->recentStateTransactionSizes.size()).detail("StateBytes", stateBytes).detail("FirstVersion", self->recentStateTransactionSizes.empty() ? -1 : self->recentStateTransactionSizes.front().first).detail("StateMutationsIn", req.txnStateTransactions.size()).detail("StateMutationsOut", reply.stateMutations.size()).detail("From", proxyAddress);

		ASSERT(!proxyInfo.outstandingBatches.empty());
		ASSERT(self->proxyInfoMap.size() <= self->commitProxyCount + 1);

		// SOMEDAY: This is O(n) in number of proxies. O(log n) solution using appropriate data structure?
		Version oldestProxyVersion = req.version;
		for (auto itr = self->proxyInfoMap.begin(); itr != self->proxyInfoMap.end(); ++itr) {
			//TraceEvent("ResolveBatchProxyVersion", self->dbgid).detail("CommitProxy", itr->first).detail("Version", itr->second.lastVersion);
			if (itr->first.isValid()) { // Don't consider the first master request
				oldestProxyVersion = std::min(itr->second.lastVersion, oldestProxyVersion);
			} else {
				// The master's request version should never prevent us from clearing recentStateTransactions
				ASSERT(self->debugMinRecentStateVersion == 0 ||
				       self->debugMinRecentStateVersion > itr->second.lastVersion);
			}
		}

		TEST(oldestProxyVersion == req.version); // The proxy that sent this request has the oldest current version
		TEST(oldestProxyVersion !=
		     req.version); // The proxy that sent this request does not have the oldest current version

		bool anyPopped = false;
		if (firstUnseenVersion <= oldestProxyVersion && self->proxyInfoMap.size() == self->commitProxyCount + 1) {
			TEST(true); // Deleting old state transactions
			self->recentStateTransactions.erase(self->recentStateTransactions.begin(),
			                                    self->recentStateTransactions.upper_bound(oldestProxyVersion));
			self->debugMinRecentStateVersion = oldestProxyVersion + 1;

			while (self->recentStateTransactionSizes.size() &&
			       self->recentStateTransactionSizes.front().first <= oldestProxyVersion) {
				anyPopped = true;
				stateBytes -= self->recentStateTransactionSizes.front().second;
				self->recentStateTransactionSizes.pop_front();
			}
		}

		self->version.set(req.version);
		bool breachedLimit = self->totalStateBytes.get() <= SERVER_KNOBS->RESOLVER_STATE_MEMORY_LIMIT &&
		                     self->totalStateBytes.get() + stateBytes > SERVER_KNOBS->RESOLVER_STATE_MEMORY_LIMIT;
		self->totalStateBytes.setUnconditional(self->totalStateBytes.get() + stateBytes);
		if (anyPopped || breachedLimit) {
			self->checkNeededVersion.trigger();
		}

		if (req.debugID.present())
			g_traceBatch.addEvent("CommitDebug", debugID.get().first(), "Resolver.resolveBatch.After");
	} else {
		TEST(true); // Duplicate resolve batch request
		//TraceEvent("DupResolveBatchReq", self->dbgid).detail("From", proxyAddress);
	}

	auto proxyInfoItr = self->proxyInfoMap.find(proxyAddress);

	if (proxyInfoItr != self->proxyInfoMap.end()) {
		auto batchItr = proxyInfoItr->second.outstandingBatches.find(req.version);
		if (batchItr != proxyInfoItr->second.outstandingBatches.end()) {
			req.reply.send(batchItr->second);
		} else {
			TEST(true); // No outstanding batches for version on proxy
			req.reply.send(Never());
		}
	} else {
		ASSERT_WE_THINK(false); // The first non-duplicate request with this proxyAddress, including this one, should
		                        // have inserted this item in the map!
		// TEST(true); // No prior proxy requests
		req.reply.send(Never());
	}

	++self->resolveBatchOut;

	return Void();
}

namespace {

// TODO: refactor with the one in CommitProxyServer.actor.cpp
struct TransactionStateResolveContext {
	// Maximum sequence for txnStateRequest, this is defined when the request last flag is set.
	Sequence maxSequence = std::numeric_limits<Sequence>::max();

	// Flags marks received transaction state requests, we only process the transaction request when *all* requests are
	// received.
	std::unordered_set<Sequence> receivedSequences;

	Reference<Resolver> pResolverData;

	// Pointer to transaction state store, shortcut for commitData.txnStateStore
	IKeyValueStore* pTxnStateStore = nullptr;

	// Actor streams
	PromiseStream<Future<Void>>* pActors = nullptr;

	// Flag reports if the transaction state request is complete. This request should only happen during recover, i.e.
	// once per Resolver.
	bool processed = false;

	TransactionStateResolveContext() = default;

	TransactionStateResolveContext(Reference<Resolver> pResolverData_, PromiseStream<Future<Void>>* pActors_)
	  : pResolverData(pResolverData_), pTxnStateStore(pResolverData_->txnStateStore), pActors(pActors_) {
		ASSERT(pTxnStateStore != nullptr || !SERVER_KNOBS->PROXY_USE_RESOLVER_PRIVATE_MUTATIONS);
	}
};

ACTOR Future<Void> processCompleteTransactionStateRequest(TransactionStateResolveContext* pContext) {
	state KeyRange txnKeys = allKeys;
	state std::map<Tag, UID> tag_uid;

	RangeResult UIDtoTagMap = pContext->pTxnStateStore->readRange(serverTagKeys).get();
	for (const KeyValueRef& kv : UIDtoTagMap) {
		tag_uid[decodeServerTagValue(kv.value)] = decodeServerTagKey(kv.key);
	}

	loop {
		wait(yield());

		RangeResult data =
		    pContext->pTxnStateStore
		        ->readRange(txnKeys, SERVER_KNOBS->BUGGIFIED_ROW_LIMIT, SERVER_KNOBS->APPLY_MUTATION_BYTES)
		        .get();
		if (!data.size())
			break;

		((KeyRangeRef&)txnKeys) = KeyRangeRef(keyAfter(data.back().key, txnKeys.arena()), txnKeys.end);

		MutationsVec mutations;
		std::vector<std::pair<MapPair<Key, ServerCacheInfo>, int>> keyInfoData;
		std::vector<UID> src, dest;
		ServerCacheInfo info;
		// NOTE: An ACTOR will be compiled into several classes, the this pointer is from one of them.
		auto updateTagInfo = [this](const std::vector<UID>& uids,
		                            std::vector<Tag>& tags,
		                            std::vector<Reference<StorageInfo>>& storageInfoItems) {
			for (const auto& id : uids) {
				auto storageInfo = getStorageInfo(id, &pContext->pResolverData->storageCache, pContext->pTxnStateStore);
				ASSERT(storageInfo->tag != invalidTag);
				tags.push_back(storageInfo->tag);
				storageInfoItems.push_back(storageInfo);
			}
		};
		for (auto& kv : data) {
			if (!kv.key.startsWith(keyServersPrefix)) {
				mutations.emplace_back(mutations.arena(), MutationRef::SetValue, kv.key, kv.value);
				continue;
			}

			KeyRef k = kv.key.removePrefix(keyServersPrefix);
			if (k == allKeys.end) {
				continue;
			}
			decodeKeyServersValue(tag_uid, kv.value, src, dest);

			info.tags.clear();

			info.src_info.clear();
			updateTagInfo(src, info.tags, info.src_info);

			info.dest_info.clear();
			updateTagInfo(dest, info.tags, info.dest_info);

			uniquify(info.tags);
			keyInfoData.emplace_back(MapPair<Key, ServerCacheInfo>(k, info), 1);
		}

		// insert keyTag data separately from metadata mutations so that we can do one bulk insert which
		// avoids a lot of map lookups.
		pContext->pResolverData->keyInfo.rawInsert(keyInfoData);

		bool confChanges; // Ignore configuration changes for initial commits.
		ResolverData resolverData(pContext->pResolverData->dbgid,
		                          pContext->pTxnStateStore,
		                          &pContext->pResolverData->keyInfo,
		                          &confChanges,
		                          pContext->pResolverData->tLogGroupCollection);

		applyMetadataMutations(SpanID(), resolverData, mutations);
	} // loop

	auto lockedKey = pContext->pTxnStateStore->readValue(databaseLockedKey).get();
	// pContext->pCommitData->locked = lockedKey.present() && lockedKey.get().size();
	// pContext->pCommitData->metadataVersion = pContext->pTxnStateStore->readValue(metadataVersionKey).get();

	pContext->pTxnStateStore->enableSnapshot();

	return Void();
}

ACTOR Future<Void> processTransactionStateRequestPart(TransactionStateResolveContext* pContext,
                                                      TxnStateRequest request) {
	ASSERT(pContext->pResolverData.getPtr() != nullptr);
	ASSERT(pContext->pActors != nullptr);

	if (pContext->receivedSequences.count(request.sequence)) {
		// This part is already received. Still we will re-broadcast it to other CommitProxies & Resolvers
		pContext->pActors->send(broadcastTxnRequest(request, SERVER_KNOBS->TXN_STATE_SEND_AMOUNT, true));
		wait(yield());
		return Void();
	}

	if (request.last) {
		// This is the last piece of subsequence, yet other pieces might still on the way.
		pContext->maxSequence = request.sequence + 1;
	}
	pContext->receivedSequences.insert(request.sequence);

	// ASSERT(!pContext->pResolverData->validState.isSet());

	for (auto& kv : request.data) {
		pContext->pTxnStateStore->set(kv, &request.arena);
	}
	pContext->pTxnStateStore->commit(true);

	if (pContext->receivedSequences.size() == pContext->maxSequence) {
		// Received all components of the txnStateRequest
		ASSERT(!pContext->processed);
		wait(processCompleteTransactionStateRequest(pContext));
		pContext->processed = true;
	}

	pContext->pActors->send(broadcastTxnRequest(request, SERVER_KNOBS->TXN_STATE_SEND_AMOUNT, true));
	wait(yield());
	return Void();
}

} // anonymous namespace

ACTOR Future<Void> resolverCore(ResolverInterface resolver,
                                InitializeResolverRequest initReq,
                                Reference<AsyncVar<ServerDBInfo> const> db) {
	state Reference<Resolver> self(new Resolver(resolver.id(), initReq.commitProxyCount, initReq.resolverCount));
	state ActorCollection actors(false);
	state Future<Void> doPollMetrics = self->resolverCount > 1 ? Void() : Future<Void>(Never());
	actors.add(waitFailureServer(resolver.waitFailure.getFuture()));
	actors.add(traceRole(Role::RESOLVER, resolver.id()));

	TraceEvent("ResolverInit", resolver.id()).detail("RecoveryCount", initReq.recoveryCount);

	// Wait until we can load the "real" logsystem, since we don't support switching them currently
	while (!(db->get().master.id() == initReq.masterId &&
	         db->get().recoveryState >= RecoveryState::RECOVERY_TRANSACTION)) {
		//TraceEvent("ResolverInit2", resolver.id()).detail("LSEpoch", db->get().logSystemConfig.epoch);
		wait(db->onChange());
	}

	// Initialize txnStateStore
	self->logSystem = ILogSystem::fromServerDBInfo(resolver.id(), db->get(), false, addActor);

	state PromiseStream<Future<Void>> addActor;
	state Future<Void> onError =
	    transformError(actorCollection(addActor.getFuture()), broken_promise(), master_resolver_failed());
	state TransactionStateResolveContext transactionStateResolveContext;
	if (SERVER_KNOBS->PROXY_USE_RESOLVER_PRIVATE_MUTATIONS) {
		self->logAdapter = new LogSystemDiskQueueAdapter(self->logSystem, Reference<AsyncVar<PeekTxsInfo>>(), 1, false);
		self->txnStateStore = keyValueStoreLogSystem(self->logAdapter, resolver.id(), 2e9, true, true, true);

		// wait for txnStateStore recovery
		wait(success(self->txnStateStore->readValue(StringRef())));

		// This has to be declared after the self->txnStateStore get initialized
		transactionStateResolveContext = TransactionStateResolveContext(self, &addActor);

		// Add TLog groups to collection
		self->tLogGroupCollection = makeReference<TLogGroupCollection>(db);
		const auto& logset = db->get().logSystemConfig.tLogs[0];
		for (const auto& gid : logset.tLogGroupIDs) {
			TLogGroupRef group = makeReference<TLogGroup>(gid);
			self->tLogGroupCollection->addTLogGroup(self->dbgid, group);
		}
	}

	loop choose {
		when(ResolveTransactionBatchRequest batch = waitNext(resolver.resolve.getFuture())) {
			actors.add(resolveBatch(self, batch));
		}
		when(ResolutionMetricsRequest req = waitNext(resolver.metrics.getFuture())) {
			++self->metricsRequests;
			req.reply.send(self->iopsSample.getEstimate(SERVER_KNOBS->PROXY_USE_RESOLVER_PRIVATE_MUTATIONS ? normalKeys
			                                                                                               : allKeys));
		}
		when(ResolutionSplitRequest req = waitNext(resolver.split.getFuture())) {
			++self->splitRequests;
			ResolutionSplitReply rep;
			rep.key = self->iopsSample.splitEstimate(req.range, req.offset, req.front);
			rep.used = self->iopsSample.getEstimate(req.front ? KeyRangeRef(req.range.begin, rep.key)
			                                                  : KeyRangeRef(rep.key, req.range.end));
			req.reply.send(rep);
		}
		when(wait(actors.getResult())) {}
		when(wait(onError)) {}
		when(wait(doPollMetrics)) {
			self->iopsSample.poll();
			doPollMetrics = delay(SERVER_KNOBS->SAMPLE_POLL_TIME);
		}
		when(TxnStateRequest request = waitNext(resolver.txnState.getFuture())) {
			if (SERVER_KNOBS->PROXY_USE_RESOLVER_PRIVATE_MUTATIONS) {
				addActor.send(processTransactionStateRequestPart(&transactionStateResolveContext, request));
			} else {
				ASSERT(false);
			}
		}
	}
}

ACTOR Future<Void> checkRemoved(Reference<AsyncVar<ServerDBInfo> const> db,
                                uint64_t recoveryCount,
                                ResolverInterface myInterface) {
	loop {
		if (db->get().recoveryCount >= recoveryCount &&
		    !std::count(db->get().resolvers.begin(), db->get().resolvers.end(), myInterface))
			throw worker_removed();
		wait(db->onChange());
	}
}

ACTOR Future<Void> resolver(ResolverInterface resolver,
                            InitializeResolverRequest initReq,
                            Reference<AsyncVar<ServerDBInfo> const> db) {
	try {
		state Future<Void> core = resolverCore(resolver, initReq, db);
		loop choose {
			when(wait(core)) { return Void(); }
			when(wait(checkRemoved(db, initReq.recoveryCount, resolver))) {}
		}
	} catch (Error& e) {
		if (e.code() == error_code_actor_cancelled || e.code() == error_code_worker_removed) {
			TraceEvent("ResolverTerminated", resolver.id()).error(e, true);
			return Void();
		}
		throw;
	}
}
