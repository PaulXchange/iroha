/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "main/impl/consensus_init.hpp"

#include "consensus/yac/consistency_model.hpp"
#include "consensus/yac/impl/peer_orderer_impl.hpp"
#include "consensus/yac/impl/timer_impl.hpp"
#include "consensus/yac/impl/yac_crypto_provider_impl.hpp"
#include "consensus/yac/impl/yac_gate_impl.hpp"
#include "consensus/yac/impl/yac_hash_provider_impl.hpp"
#include "consensus/yac/storage/yac_proposal_storage.hpp"
#include "consensus/yac/transport/impl/network_impl.hpp"

namespace iroha {
  namespace consensus {
    namespace yac {

      auto createPeerOrderer(
          std::shared_ptr<ametsuchi::PeerQueryFactory> peer_query_factory) {
        return std::make_shared<PeerOrdererImpl>(peer_query_factory);
      }

      auto createCryptoProvider(
          const shared_model::crypto::Keypair &keypair,
          std::shared_ptr<shared_model::interface::CommonObjectsFactory>
              common_objects_factory) {
        auto crypto = std::make_shared<CryptoProviderImpl>(
            keypair, std::move(common_objects_factory));

        return crypto;
      }

      auto createTimer(std::chrono::milliseconds delay_milliseconds) {
        return std::make_shared<TimerImpl>([delay_milliseconds] {
          // static factory with a single thread
          //
          // observe_on_new_thread -- coordination which creates new thread with
          // observe_on strategy -- all subsequent operations will be performed
          // on this thread.
          //
          // scheduler owns a timeline that is exposed by the now() method.
          // scheduler is also a factory for workers in that timeline.
          //
          // coordination is a factory for coordinators and has a scheduler.
          //
          // coordinator has a worker, and is a factory for coordinated
          // observables, subscribers and schedulable functions.
          //
          // A new thread scheduler is created
          // by calling .create_coordinator().get_scheduler()
          //
          // static allows to reuse the same thread in subsequent calls to this
          // lambda
          static rxcpp::observe_on_one_worker coordination(
              rxcpp::observe_on_new_thread()
                  .create_coordinator()
                  .get_scheduler());
          return rxcpp::observable<>::timer(
              std::chrono::milliseconds(delay_milliseconds), coordination);
        });
      }

      auto createHashProvider() {
        return std::make_shared<YacHashProviderImpl>();
      }

      std::shared_ptr<consensus::yac::Yac> createYac(
          ClusterOrdering initial_order,
          const shared_model::crypto::Keypair &keypair,
          std::chrono::milliseconds delay_milliseconds,
          std::shared_ptr<YacNetwork> network,
          std::shared_ptr<shared_model::interface::CommonObjectsFactory>
              common_objects_factory,
          ConsistencyModel consistency_model) {
        return Yac::create(
            YacVoteStorage(consistency_model),
            std::move(network),
            createCryptoProvider(keypair, std::move(common_objects_factory)),
            createTimer(delay_milliseconds),
            initial_order);
      }

      std::shared_ptr<YacGate> YacInit::initConsensusGate(
          std::shared_ptr<ametsuchi::PeerQueryFactory> peer_query_factory,
          std::shared_ptr<simulator::BlockCreator> block_creator,
          std::shared_ptr<network::BlockLoader> block_loader,
          const shared_model::crypto::Keypair &keypair,
          std::shared_ptr<consensus::ConsensusResultCache>
              consensus_result_cache,
          std::chrono::milliseconds vote_delay_milliseconds,
          std::shared_ptr<
              iroha::network::AsyncGrpcClient<google::protobuf::Empty>>
              async_call,
          std::shared_ptr<shared_model::interface::CommonObjectsFactory>
              common_objects_factory,
          ConsistencyModel consistency_model) {
        auto peer_orderer = createPeerOrderer(peer_query_factory);

        consensus_network_ = std::make_shared<NetworkImpl>(async_call);

        auto yac = createYac(peer_orderer->getInitialOrdering().value(),
                             keypair,
                             vote_delay_milliseconds,
                             consensus_network_,
                             std::move(common_objects_factory),
                             consistency_model);
        consensus_network_->subscribe(yac);

        auto hash_provider = createHashProvider();

        initialized_ = true;

        return std::make_shared<YacGateImpl>(std::move(yac),
                                             std::move(peer_orderer),
                                             hash_provider,
                                             block_creator,
                                             std::move(consensus_result_cache));
      }

      std::shared_ptr<NetworkImpl> YacInit::getConsensusNetwork() const {
        BOOST_ASSERT_MSG(initialized_,
                         "YacInit::initConsensusGate(...) must be called prior "
                         "to YacInit::getConsensusNetwork()!");
        return consensus_network_;
      }
    }  // namespace yac
  }    // namespace consensus
}  // namespace iroha
