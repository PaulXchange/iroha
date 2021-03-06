/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IROHA_ORDERING_GATE_IMPL_HPP
#define IROHA_ORDERING_GATE_IMPL_HPP

#include "network/ordering_gate.hpp"

#include <mutex>

#include <tbb/concurrent_priority_queue.h>

#include "interfaces/common_objects/types.hpp"
#include "logger/logger.hpp"
#include "network/impl/async_grpc_client.hpp"
#include "network/ordering_gate_transport.hpp"

namespace shared_model {
  namespace interface {
    class Proposal;
  }  // namespace interface
}  // namespace shared_model

namespace iroha {
  namespace ordering {

    /**
     * Compare proposals by height
     */
    struct ProposalComparator {
      bool operator()(
          const std::shared_ptr<shared_model::interface::Proposal> &lhs,
          const std::shared_ptr<shared_model::interface::Proposal> &rhs) const;
    };

    /**
     * OrderingGate implementation with gRPC asynchronous client
     * Interacts with given OrderingService
     * by propagating transactions and receiving proposals
     * @param server_address OrderingService address
     */
    class OrderingGateImpl : public network::OrderingGate,
                             public network::OrderingGateNotification {
     public:
      /**
       * @param transport - network communication layer
       * @param initial_height - height of the last block stored on this peer
       * @param run_async - whether proposals should be handled
       * asynchronously (on separate thread). Default is true.
       * @param log to print progress
       */
      OrderingGateImpl(
          std::shared_ptr<iroha::network::OrderingGateTransport> transport,
          shared_model::interface::types::HeightType initial_height,
          bool run_async = true,
          logger::Logger log = logger::log("OrderingGate"));

      void propagateBatch(
          std::shared_ptr<shared_model::interface::TransactionBatch> batch)
          override;

      rxcpp::observable<network::OrderingEvent> onProposal() override;

      void setPcs(const iroha::network::PeerCommunicationService &pcs) override;

      void onProposal(
          std::shared_ptr<shared_model::interface::Proposal> proposal) override;

      ~OrderingGateImpl() override;

     private:
      /**
       * Try to push proposal for next consensus round
       * @param - last_block_height - what is the last block stored on this
       * peer, or for which commit was received. If block is newer than
       * currently stored proposals, proposals are discarded. If it is older,
       * newer proposals are propagated in order
       */
      void tryNextRound(
          shared_model::interface::types::HeightType last_block_height);

      rxcpp::subjects::subject<network::OrderingEvent> proposals_;

      /**
       * Notification subject which is used only for notification purposes
       * without semantic for emitted values
       */
      rxcpp::subjects::subject<shared_model::interface::types::HeightType>
          net_proposals_;
      std::shared_ptr<iroha::network::OrderingGateTransport> transport_;

      std::mutex proposal_mutex_;

      /// queue with all proposals received from ordering service
      tbb::concurrent_priority_queue<
          std::shared_ptr<shared_model::interface::Proposal>,
          ProposalComparator>
          proposal_queue_;

      /// last commited block height
      shared_model::interface::types::HeightType last_block_height_;

      /// subscription of pcs::on_commit
      rxcpp::composite_subscription pcs_subscriber_;

      logger::Logger log_;

      bool run_async_;
    };
  }  // namespace ordering
}  // namespace iroha

#endif  // IROHA_ORDERING_GATE_IMPL_HPP
