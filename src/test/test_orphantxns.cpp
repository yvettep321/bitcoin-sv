// Copyright (c) 2019 The Bitcoin SV developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "config.h"
#include "net/net_processing.h"
#include "orphan_txns.h"
#include "test/test_bitcoin.h"

#include <random>
#include <boost/test/unit_test.hpp>

namespace {
    CService ip(uint32_t i) {
        struct in_addr s;
        s.s_addr = i;
        return CService(CNetAddr(s), Params().GetDefaultPort());
    }
    // Use a default configuration
    size_t maxCollectedOutpoints = COrphanTxns::DEFAULT_MAX_COLLECTED_OUTPOINTS;
    size_t maxExtraTxnsForCompactBlock = COrphanTxns::DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN;
    size_t maxTxSizePolicy = DEFAULT_MAX_TX_SIZE_POLICY_AFTER_GENESIS;
    size_t maxOrphanPercent = COrphanTxns::DEFAULT_MAX_PERCENTAGE_OF_ORPHANS_IN_BATCH;
    size_t maxInputsOutputs = COrphanTxns::DEFAULT_MAX_INPUTS_OUTPUTS_PER_TRANSACTION;

    // Create txn inputs
    std::vector<CTxIn> CreateTxnInputs(size_t nNumOfInputs, uint256 txid=InsecureRand256()) {
        std::vector<CTxIn> vin;
        vin.resize(nNumOfInputs);
        for (size_t idx=0; idx<nNumOfInputs; ++idx) {
            vin[idx].prevout = COutPoint(txid, idx);
            vin[idx].scriptSig << OP_1;
        }
        return vin;
    }
    // Create txn input from a given outpoints.
    std::vector<CTxIn> CreateTxnInputs(std::vector<COutPoint> vOutpoints) {
        std::vector<CTxIn> vin;
        vin.resize(vOutpoints.size());
        for (size_t idx=0; idx<vOutpoints.size(); ++idx) {
            vin[idx].prevout = vOutpoints[idx];
            vin[idx].scriptSig << OP_1;
        }
        return vin;
    }
    // Create txn outputs
    std::vector<CTxOut> CreateTxnOutputs(size_t nNumOfOutputs) {
        // Creat private keys.
        std::vector<CKey> vKey(nNumOfOutputs);
        for (auto& key: vKey) {
            key.MakeNewKey(true);
        }
        // Create outpoints
        std::vector<CTxOut> vout;
        vout.resize(nNumOfOutputs);
        for (size_t idx=0; idx<nNumOfOutputs; ++idx) {
            // A fixed value
            vout[idx].nValue = 1 * CENT;
            vout[idx].scriptPubKey = GetScriptForDestination(vKey[idx].GetPubKey().GetID());
        }
        return vout;
    }

    // Create an orphan txn
    TxInputDataSPtr CreateOrphanTxn(TxSource source,
                                    std::vector<CTxIn> vTxnInputs = CreateTxnInputs(1),
                                    std::vector<CTxOut> vTxnOutputs = CreateTxnOutputs(1),
                                    std::shared_ptr<CNode> pNode=nullptr) {
        CMutableTransaction tx;
        tx.vin = vTxnInputs;
        tx.vout = vTxnOutputs;
        // Return txn's input data
        return std::make_shared<CTxInputData>(
                   g_connman->GetTxIdTracker(),
                   MakeTransactionRef(tx), // a pointer to the tx
                   source,   // tx source
                   TxValidationPriority::normal, // tx validation priority
                   TxStorage::memory, // tx storage
                   0,        // nAcceptTime
                   Amount(0), // nAbsurdFee
                   pNode);   // pNode
    }
    // Populate orphan txn's object with a given number of txns.
    void OrphanTxnsObjectCreateNOrphanTxns(
        std::shared_ptr<COrphanTxns>& orphanTxns,
        TxSource source,
        int32_t nOrphanTxnsCount,
        CConnman::CAsyncTaskPool& asyncTaskPool,
        std::vector<CNodePtr>& nodes)
    {
        nodes.clear();
        for (NodeId i = 0; i < nOrphanTxnsCount; i++) {
            CAddress dummy_addr(ip(0xa0b0c001), NODE_NONE);
            CNodePtr pNode =
                CNode::Make(
                    i,
                    NODE_NETWORK,
                    0,
                    INVALID_SOCKET,
                    dummy_addr,
                    0u,
                    0u,
                    asyncTaskPool,
                    "",
                    true);
            nodes.push_back(pNode);
            // Create txn and add it to the queue
            orphanTxns->addTxn(CreateOrphanTxn(source, CreateTxnInputs(1), CreateTxnOutputs(1), pNode));
        }
    }
}

BOOST_FIXTURE_TEST_SUITE(test_orphantxns, TestingSetup)

BOOST_AUTO_TEST_CASE(test_orphantxns_creation) {
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                maxOrphanPercent,
                maxInputsOutputs)
    };
    BOOST_REQUIRE(orphanTxns);
}

BOOST_AUTO_TEST_CASE(test_orphantxns_addtxn_erasetxns) {
    CConnman::CAsyncTaskPool asyncTaskPool{GlobalConfig::GetConfig()};
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                maxOrphanPercent,
                maxInputsOutputs)
    };
    size_t nTxnsNumber=10;
    // Create orphan transactions:
    std::vector<CNodePtr> nodes {};
    OrphanTxnsObjectCreateNOrphanTxns(orphanTxns, TxSource::p2p, nTxnsNumber, asyncTaskPool, nodes);
    // Check txns count
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Erase all txns
    orphanTxns->eraseTxns();
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 0);
}

BOOST_AUTO_TEST_CASE(test_orphantxns_limit_txns_size) {
    CConnman::CAsyncTaskPool asyncTaskPool{GlobalConfig::GetConfig()};
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                maxOrphanPercent,
                maxInputsOutputs)
    };
    size_t nTxnsNumber=1000;
    CAddress dummy_addr(ip(0xa0b0c001), NODE_NONE);
    // Create orphan transactions:
    std::vector<CNodePtr> nodes {};
    OrphanTxnsObjectCreateNOrphanTxns(orphanTxns, TxSource::p2p, nTxnsNumber, asyncTaskPool, nodes);
    // Check txns count
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Test limit function: (each generated transaction is 86 bytes long)
    orphanTxns->limitTxnsSize(86000, 0);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 1000);
    orphanTxns->limitTxnsSize(860, 0);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 10);
     orphanTxns->limitTxnsSize(859, 0);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 9);
    orphanTxns->limitTxnsSize(86, 0);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 1);
    orphanTxns->limitTxnsSize(85, 0);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 0);
    orphanTxns->limitTxnsSize(0, 0);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 0);
}

BOOST_AUTO_TEST_CASE(test_orphantxns_checktxnexists) {
    CConnman::CAsyncTaskPool asyncTaskPool{GlobalConfig::GetConfig()};
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                maxOrphanPercent,
                maxInputsOutputs)
    };
    size_t nTxnsNumber=10;
    // Create orphan transactions:
    std::vector<CNodePtr> nodes {};
    OrphanTxnsObjectCreateNOrphanTxns(orphanTxns, TxSource::p2p, nTxnsNumber, asyncTaskPool, nodes);
    // Check txns count
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Create a txns which is not present in queue
    auto txn = CreateOrphanTxn(TxSource::p2p);
    BOOST_CHECK(!orphanTxns->checkTxnExists(txn->GetTxnPtr()->GetId()));
    orphanTxns->addTxn(txn);
    BOOST_CHECK(orphanTxns->checkTxnExists(txn->GetTxnPtr()->GetId()));
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber+1);
}

BOOST_AUTO_TEST_CASE(test_orphantxns_erasetxn) {
    CConnman::CAsyncTaskPool asyncTaskPool{GlobalConfig::GetConfig()};
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                maxOrphanPercent,
                maxInputsOutputs)
    };
    size_t nTxnsNumber=10;
    // Create orphan transactions:
    std::vector<CNodePtr> nodes {};
    OrphanTxnsObjectCreateNOrphanTxns(orphanTxns, TxSource::p2p, nTxnsNumber, asyncTaskPool, nodes);
    // Check txns count
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Create a txns which is not present in queue
    auto txn = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber+1);
    auto txnToErase = txn->GetTxnPtr()->GetId();
    // Erase a given txn
    orphanTxns->eraseTxn(txnToErase);
    // Check if txn was erased
    BOOST_CHECK(!orphanTxns->checkTxnExists(txnToErase));
    // Check if a total number of txns is changed
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
}

BOOST_AUTO_TEST_CASE(test_orphantxns_erasetxnfrompeer) {
    CConnman::CAsyncTaskPool asyncTaskPool{GlobalConfig::GetConfig()};
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                maxOrphanPercent,
                maxInputsOutputs)
    };
    size_t nTxnsNumber=10;
    size_t nNodesNumber=10;
    // Create orphan transactions:
    std::vector<CNodePtr> nodes {};
    OrphanTxnsObjectCreateNOrphanTxns(orphanTxns, TxSource::p2p, nTxnsNumber, asyncTaskPool, nodes);
    // Check txns count
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Erase txns from a node which is not connected (there are no orphan txns from this node)
    orphanTxns->eraseTxnsFromPeer((NodeId)(nNodesNumber+1));
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Erase all txns from Node0
    orphanTxns->eraseTxnsFromPeer((NodeId)(0));
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber-1);
    // Delete txns from all other nodes
    for (NodeId nodeId=1; nodeId < (NodeId)nNodesNumber; nodeId++) {
        orphanTxns->eraseTxnsFromPeer((NodeId)nodeId);
    }
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 0);
}

BOOST_AUTO_TEST_CASE(test_gettxids) {
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                maxOrphanPercent,
                maxInputsOutputs)
    };
    // Create orphan transactions:
    auto txn1 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn1);
    auto txn2 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn2);
    auto txn3 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn3);
    // Store txids in the vector.
    std::vector<TxId> vKnownTxIds {
        txn1->GetTxnPtr()->GetId(),
        txn2->GetTxnPtr()->GetId(),
        txn3->GetTxnPtr()->GetId(),
    };
    std::sort(vKnownTxIds.begin(), vKnownTxIds.end());
    // Get txids via getTxIds call.
    auto vTxIds = orphanTxns->getTxIds();
    std::sort(vTxIds.begin(), vTxIds.end());
    // Check if vectors are equal.
    BOOST_CHECK(vKnownTxIds == vTxIds);
}

BOOST_AUTO_TEST_CASE(test_orphantxns_maxcollectedoutpoints) {
    size_t nMaxCollectedOutpoints = 100;
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                nMaxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                maxOrphanPercent,
                maxInputsOutputs)
    };
    // Create txn with a max number of outpoints the OrphanTxn can collect
    auto txn1 = CreateOrphanTxn(
                    TxSource::p2p,
                    CreateTxnInputs(1),
                    CreateTxnOutputs(nMaxCollectedOutpoints));
    // Create a vector with expected outpoints from txn1
    std::vector<COutPoint> vExpectedOutpoints {};
    auto txn1id = txn1->GetTxnPtr()->GetId();
    for (size_t i=0; i<nMaxCollectedOutpoints; ++i) {
        vExpectedOutpoints.emplace_back(COutPoint{txn1id, (uint32_t)i});
    }
    // Collect outpoints from txn1
    orphanTxns->collectTxnOutpoints(*(txn1->GetTxnPtr()));
    // Get collected outpoints
    auto vReturnedOutpoints = orphanTxns->getCollectedOutpoints();
    BOOST_CHECK(
        std::equal(vExpectedOutpoints.begin(),
                   vExpectedOutpoints.end(),
                   vReturnedOutpoints.begin()));
    BOOST_CHECK(vReturnedOutpoints.size() == nMaxCollectedOutpoints);

    // Collect outpoints from a txn which creates nTxnNumOfOutpoints number of outpoints,
    // where nTxnNumOfOutpoints is a random number from the range [1, nMaxCollectedOutpoints+1].
    // The following loop helps to check if outpoints are collected properly.
    std::mt19937 random_engine;
    std::uniform_int_distribution<int> distribution(1, nMaxCollectedOutpoints+1);
    for (int j=0; j<100; ++j) {
        size_t nTxnNumOfOutpoints = distribution(random_engine);
        auto txn = CreateOrphanTxn(
                       TxSource::p2p,
                       CreateTxnInputs(1),
                       CreateTxnOutputs(nTxnNumOfOutpoints));
        orphanTxns->collectTxnOutpoints(*(txn->GetTxnPtr()));
        // Check if rotate can be applied to remove the oldest outpoints
        if (nTxnNumOfOutpoints < nMaxCollectedOutpoints) {
                std::rotate(
                    vExpectedOutpoints.begin(),
                    vExpectedOutpoints.begin() + nTxnNumOfOutpoints,
                    vExpectedOutpoints.end());
            vExpectedOutpoints.resize(vExpectedOutpoints.size() - nTxnNumOfOutpoints);
        } else {
            vExpectedOutpoints.clear();
        }
        auto txnid = txn->GetTxnPtr()->GetId();
        for (size_t i=0; i<nTxnNumOfOutpoints; ++i) {
            vExpectedOutpoints.emplace_back(COutPoint{txnid, (uint32_t)i});
        }
        auto vReturnedOutpoints2 = orphanTxns->getCollectedOutpoints();
        BOOST_CHECK(
            std::equal(
                vExpectedOutpoints.begin(),
                vExpectedOutpoints.end(),
                vReturnedOutpoints2.begin()));
    }
}

BOOST_AUTO_TEST_CASE(test_orphantxns_erasecollectedoutpointsfromtxns) {
    size_t nMaxCollectedOutpoints = 100;
    size_t nTxn1NumOfOutpoints = 10;
    size_t nTxn2NumOfOutpoints = 2;
    size_t nTxn3NumOfOutpoints = 3;
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                nMaxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                maxOrphanPercent,
                maxInputsOutputs)
    };
    // Create txn1
    auto txn1 = CreateOrphanTxn(
                    TxSource::p2p,
                    CreateTxnInputs(1),
                    CreateTxnOutputs(nTxn1NumOfOutpoints));
    auto txn1id = txn1->GetTxnPtr()->GetId();
    // Create txn2
    auto txn2 = CreateOrphanTxn(
                    TxSource::p2p,
                    CreateTxnInputs(1),
                    CreateTxnOutputs(nTxn2NumOfOutpoints));
    auto txn2id = txn2->GetTxnPtr()->GetId();
    // Create txn3
    auto txn3 = CreateOrphanTxn(
                    TxSource::p2p,
                    CreateTxnInputs(1),
                    CreateTxnOutputs(nTxn3NumOfOutpoints));
    auto txn3id = txn3->GetTxnPtr()->GetId();
    // Create a vector with expected outpoints from txn1
    std::vector<COutPoint> vTxn1ExpectedOutpoints {};
    for (size_t i=0; i<nTxn1NumOfOutpoints; ++i) {
        vTxn1ExpectedOutpoints.emplace_back(COutPoint{txn1id, (uint32_t)i});
    }
    // Create a vector with expected outpoints from txn3
    std::vector<COutPoint> vTxn3ExpectedOutpoints {};
    for (size_t i=0; i<nTxn3NumOfOutpoints; ++i) {
        vTxn3ExpectedOutpoints.emplace_back(COutPoint{txn3id, (uint32_t)i});
    }
    // Collect outpoints from txn1. Then, remove all outpoints from txn1
    {
        orphanTxns->collectTxnOutpoints(*(txn1->GetTxnPtr()));
        orphanTxns->eraseCollectedOutpointsFromTxns(std::vector<TxId>{txn1id});
        auto vReturnedOutpoints = orphanTxns->getCollectedOutpoints();
        BOOST_CHECK(vReturnedOutpoints.empty());
    }
    // Collect outpoints from txn1 & txn2. Then, remove outpoints from txn2
    {
        orphanTxns->collectTxnOutpoints(*(txn1->GetTxnPtr()));
        orphanTxns->collectTxnOutpoints(*(txn2->GetTxnPtr()));
        orphanTxns->eraseCollectedOutpointsFromTxns(std::vector<TxId>{txn2id});
        auto vReturnedOutpoints = orphanTxns->getCollectedOutpoints();
        BOOST_CHECK(vReturnedOutpoints.size() == nTxn1NumOfOutpoints);
        BOOST_CHECK(
            std::equal(
                vTxn1ExpectedOutpoints.begin(),
                vTxn1ExpectedOutpoints.end(),
                vReturnedOutpoints.begin()));
    }
    // Erase previously collected outpoints
    orphanTxns->eraseCollectedOutpoints();
    // Collect outpoints from txn1, txn2 & txn3. Then, remove outpoints from txn2
    {
        orphanTxns->collectTxnOutpoints(*(txn1->GetTxnPtr()));
        orphanTxns->collectTxnOutpoints(*(txn2->GetTxnPtr()));
        orphanTxns->collectTxnOutpoints(*(txn3->GetTxnPtr()));
        orphanTxns->eraseCollectedOutpointsFromTxns(std::vector<TxId>{txn2id});
        auto vReturnedOutpoints = orphanTxns->getCollectedOutpoints();
        BOOST_CHECK(vReturnedOutpoints.size() == nTxn1NumOfOutpoints + nTxn3NumOfOutpoints);
        // Get outpoints from txn1 & txn3
        auto vTxn1AndTxn3ExpectedOutpoints = vTxn1ExpectedOutpoints;
        vTxn1AndTxn3ExpectedOutpoints.insert(
                vTxn1AndTxn3ExpectedOutpoints.end(),
                vTxn3ExpectedOutpoints.begin(),
                vTxn3ExpectedOutpoints.end());
        BOOST_CHECK(
            std::equal(
                vTxn1AndTxn3ExpectedOutpoints.begin(),
                vTxn1AndTxn3ExpectedOutpoints.end(),
                vReturnedOutpoints.begin()));
    }
}

BOOST_AUTO_TEST_CASE(test_orphantxns_collectdependenttxnsforretry) {
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                maxOrphanPercent,
                maxInputsOutputs)
    };
    size_t nTxnsNumber=5;
    // Create orphan transactions:
    auto txn1 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn1);
    auto txn2 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn2);
    auto txn3 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn3);
    auto txn4 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn4);
    auto txn5 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn5);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber);
    // Create dependent orphan txns:
    // dependent_txn1 takes txn1 as an input.
    auto dependent_txn1 {
        CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(txn1->GetTxnPtr()->GetId(), 0)}))
    };
    orphanTxns->addTxn(dependent_txn1);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber+1);
    // Test case 1: Collect dependent txns for retry
    orphanTxns->collectTxnOutpoints(*(txn1->GetTxnPtr()));
    auto vOrphanTxnsForRetry = orphanTxns->collectDependentTxnsForRetry();
    // Check if txn1 was taken from the orphan set.
    size_t nDependentTxn1VoutSize = dependent_txn1->GetTxnPtr()->vout.size();
    BOOST_CHECK(vOrphanTxnsForRetry.size() == nDependentTxn1VoutSize);
    auto dependent_txn1_id = dependent_txn1->GetTxnPtr()->GetId();
    auto fdependenttxn1 {
        std::find_if(vOrphanTxnsForRetry.begin(),
                     vOrphanTxnsForRetry.end(),
                     [&dependent_txn1_id](const TxInputDataSPtr& txn) {
                            return dependent_txn1_id == txn->GetTxnPtr()->GetId();
                        })
    };
    BOOST_CHECK(fdependenttxn1 != vOrphanTxnsForRetry.end());
    // Test case 2:
    // Add one new not-dependent orphan txn.
    auto txn6 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->addTxn(txn6);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == nTxnsNumber+2);
    orphanTxns->collectTxnOutpoints(*(txn6->GetTxnPtr()));
    BOOST_CHECK(orphanTxns->collectDependentTxnsForRetry().empty());
    // Test case 3:
    // There is no newly added dependent orphan txn. There should be nothing for re-try.
    auto txn7 = CreateOrphanTxn(TxSource::p2p);
    orphanTxns->collectTxnOutpoints(*(txn7->GetTxnPtr()));
    BOOST_CHECK(orphanTxns->collectDependentTxnsForRetry().empty());
}

// In this test we have got txn1 which creates two outputs and then two child transactions txn2 and txn3
// each of them spending one of the outputs of txn1.
BOOST_AUTO_TEST_CASE(test_orphantxns_collectdependenttxnsforretry2) {
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                maxOrphanPercent,
                maxInputsOutputs)
    };
    // Create orphan transaction of type 1-2 (one input - two outputs):
    auto txn1 = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs(1), CreateTxnOutputs(2));
    // txn2 takes the first output from txn1 
    auto txn2 = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(txn1->GetTxnPtr()->GetId(), 0)}));
    // txn3 takes the second output from txn1 
    auto txn3 = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(txn1->GetTxnPtr()->GetId(), 1)}));
    // Add txn2 to orphans
    orphanTxns->addTxn(txn2);
    // Add txn3 to orphans
    orphanTxns->addTxn(txn3);
    // We presume that txn1 is submitted to the mempool so collect it's outpoints
    orphanTxns->collectTxnOutpoints(*(txn1->GetTxnPtr()));

    // Get txs that need to be reprocessed.
    auto vTxnsToReprocess = orphanTxns->collectDependentTxnsForRetry();
    BOOST_CHECK(vTxnsToReprocess.size() == 2);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 2);
    // Remove txs from the orphan pool.
    for (const auto& tx: vTxnsToReprocess) {
        orphanTxns->eraseTxn(tx->GetTxnPtr()->GetId());
    }

    // At this stage there is no orphans and collected outpoints in the queue.
    BOOST_CHECK(orphanTxns->collectDependentTxnsForRetry().empty());
    BOOST_CHECK(!orphanTxns->getTxnsNumber());
}

// In this test we are testing prevention of collecting transactions wit too many inputs
BOOST_AUTO_TEST_CASE(test_orphantxns_do_not_collect_tx_with_too_many_inputs) {
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                maxOrphanPercent,
                2)
    };
    // Create root transaction of type 1-1 (one input - one outputs):
    auto txn1 = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs(1), CreateTxnOutputs(2));
    // Create a first layer orphan with two children
    auto txn2 = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(txn1->GetTxnPtr()->GetId(), 0)}), CreateTxnOutputs(5));
    // Create a second layer orphan with 2 inputs, will be collected
    auto txn3 = CreateOrphanTxn(TxSource::p2p, 
        CreateTxnInputs({
            COutPoint(txn2->GetTxnPtr()->GetId(), 0), 
            COutPoint(txn2->GetTxnPtr()->GetId(), 1)
        })
    );
    // Create a second layer orphan with 3 inputs, will not be collected
    auto txn4 = CreateOrphanTxn(TxSource::p2p, 
        CreateTxnInputs({
            COutPoint(txn2->GetTxnPtr()->GetId(), 2), 
            COutPoint(txn2->GetTxnPtr()->GetId(), 3),
            COutPoint(txn2->GetTxnPtr()->GetId(), 4),
        })
    );
    // Add orphans to the pool
    orphanTxns->addTxn(txn2);
    orphanTxns->addTxn(txn3);
    orphanTxns->addTxn(txn4);
    // We presume that txn1 is submitted to the mempool so collect it's outpoints
    orphanTxns->collectTxnOutpoints(*(txn1->GetTxnPtr()));

    // Get txs that need to be reprocessed.
    auto vTxnsToReprocess = orphanTxns->collectDependentTxnsForRetry();
    // txn2 and txn3 should be collected
    BOOST_CHECK(vTxnsToReprocess.size() == 2);
    // txn4 should stay
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 3);
    
    // Remove collected txs from the orphan pool.
    for (const auto& tx: vTxnsToReprocess) {
        orphanTxns->eraseTxn(tx->GetTxnPtr()->GetId());
    }

    // Now pretend that tx2 has entered the mempool, tx4 is now a first layer transaction
    // and will be collected
    orphanTxns->collectTxnOutpoints(*(txn2->GetTxnPtr()));
    vTxnsToReprocess = orphanTxns->collectDependentTxnsForRetry();
    BOOST_CHECK(vTxnsToReprocess.size() == 1);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 1);

    // Remove collected txs from the orphan pool.
    for (const auto& tx: vTxnsToReprocess) {
        orphanTxns->eraseTxn(tx->GetTxnPtr()->GetId());
    }

    // At this stage there is no orphans and collected outpoints in the queue.
    BOOST_CHECK(orphanTxns->collectDependentTxnsForRetry().empty());
    BOOST_CHECK(!orphanTxns->getTxnsNumber());
}

// In this test we are testing prevention of collecting too many outputs of a single transaction
BOOST_AUTO_TEST_CASE(test_orphantxns_do_not_collect_tx_with_too_many_outputs) {
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                maxOrphanPercent,
                2)
    };
    // Create root transaction of type 1-1 (one input - one outputs):
    auto txn1 = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs(1), CreateTxnOutputs(2));
    // Create a first layer orphan with three children
    auto txn2 = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(txn1->GetTxnPtr()->GetId(), 0)}), CreateTxnOutputs(3));
    // Create three second layer orphans
    auto txn3 = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(txn2->GetTxnPtr()->GetId(), 0)}));
    auto txn4 = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(txn2->GetTxnPtr()->GetId(), 1)}));
    auto txn5 = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(txn2->GetTxnPtr()->GetId(), 2)}));

    // Add orphans to the pool
    orphanTxns->addTxn(txn2);
    orphanTxns->addTxn(txn3);
    orphanTxns->addTxn(txn4);
    orphanTxns->addTxn(txn5);

    // We presume that txn1 is submitted to the mempool so collect it's outpoints
    orphanTxns->collectTxnOutpoints(*(txn1->GetTxnPtr()));

    // Get txs that need to be reprocessed.
    auto vTxnsToReprocess = orphanTxns->collectDependentTxnsForRetry();
    // txn2, txn3 and txn4 should be collected, txn5 should not as it is third (limit is 2) output of txn2
    BOOST_CHECK(vTxnsToReprocess.size() == 3);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 4);
    
    // Remove collected txs from the orphan pool.
    for (const auto& tx: vTxnsToReprocess) {
        orphanTxns->eraseTxn(tx->GetTxnPtr()->GetId());
    }

    // Now pretend that tx2 has entered the mempool, tx5 is now a first layer transaction
    // and will be collected
    orphanTxns->collectTxnOutpoints(*(txn2->GetTxnPtr()));
    vTxnsToReprocess = orphanTxns->collectDependentTxnsForRetry();
    BOOST_CHECK(vTxnsToReprocess.size() == 1);
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 1);

    // Remove collected txs from the orphan pool.
    for (const auto& tx: vTxnsToReprocess) {
        orphanTxns->eraseTxn(tx->GetTxnPtr()->GetId());
    }

    // At this stage there is no orphans and collected outpoints in the queue.
    BOOST_CHECK(orphanTxns->collectDependentTxnsForRetry().empty());
    BOOST_CHECK(!orphanTxns->getTxnsNumber());
}

// In this test we are testing limitation on number of transactions released at once (in single batch)
BOOST_AUTO_TEST_CASE(test_orphantxns_max_percentage_in_batch) {

    const uint64_t MAX_PERCENTAGE_ORPHANS_IN_BATCH = 5;
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                MAX_PERCENTAGE_ORPHANS_IN_BATCH,
                maxInputsOutputs)
    };

    // calculate max txs released in single batch
    const auto MAX_TXS_RELEASED = 
        GetNumHighPriorityValidationThrs() * DEFAULT_MAX_STD_TXNS_PER_THREAD_RATIO * MAX_PERCENTAGE_ORPHANS_IN_BATCH / 100;
    // Number of txs in first layer should be lower than MAX_TXS_RELEASED
    const auto NUM_FIRST_LAYER_TXS = MAX_TXS_RELEASED * 3 / 4; 
    BOOST_ASSERT(NUM_FIRST_LAYER_TXS < MAX_TXS_RELEASED);
    BOOST_ASSERT(2 * NUM_FIRST_LAYER_TXS > MAX_TXS_RELEASED);
    // Create root transaction with enough outputs for first layer:
    auto txnRoot = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs(1), CreateTxnOutputs(NUM_FIRST_LAYER_TXS));
    // Create first and second layer orphans 
    for(uint64_t n = 0; n < NUM_FIRST_LAYER_TXS; n++){
        auto firstLayerTxn = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(txnRoot->GetTxnPtr()->GetId(), n)}));
        orphanTxns->addTxn(firstLayerTxn);
        auto secondLayerTxn = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(firstLayerTxn->GetTxnPtr()->GetId(), 0)}));
        orphanTxns->addTxn(secondLayerTxn);
    }
     
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 2 * NUM_FIRST_LAYER_TXS);
    // release and collect all outpoints from root txn
    orphanTxns->collectTxnOutpoints(*(txnRoot->GetTxnPtr()));
    auto vTxnsToReprocess = orphanTxns->collectDependentTxnsForRetry();
    // We could release all transactions but we are limited to MAX_TXS_RELEASED
    BOOST_CHECK(vTxnsToReprocess.size() == MAX_TXS_RELEASED);
}

// In this test we are testing limitation on number of transactions released at once (in single batch)
// when the number of the first layer transactions is higher than this limit
BOOST_AUTO_TEST_CASE(test_orphantxns_max_percentage_in_batch_first_layer) {

    const uint64_t MAX_PERCENTAGE_ORPHANS_IN_BATCH = 5;
    // Create orphan txn's object.
    std::shared_ptr<COrphanTxns> orphanTxns {
        std::make_shared<COrphanTxns>(
                maxCollectedOutpoints,
                maxExtraTxnsForCompactBlock,
                maxTxSizePolicy,
                MAX_PERCENTAGE_ORPHANS_IN_BATCH,
                maxInputsOutputs)
    };

    // calculate max txs released in single batch
    const auto MAX_TXS_RELEASED = 
        GetNumHighPriorityValidationThrs() * DEFAULT_MAX_STD_TXNS_PER_THREAD_RATIO * MAX_PERCENTAGE_ORPHANS_IN_BATCH / 100;
    // Number of txs in first layer should be higher than MAX_TXS_RELEASED
    const auto NUM_FIRST_LAYER_TXS = MAX_TXS_RELEASED * 4 / 3; 
    BOOST_ASSERT(NUM_FIRST_LAYER_TXS > MAX_TXS_RELEASED);

    // Create root transaction with enough outputs for first layer:
    auto txnRoot = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs(1), CreateTxnOutputs(NUM_FIRST_LAYER_TXS));
    // Create first and second layer orphans 
    for(uint64_t n = 0; n < NUM_FIRST_LAYER_TXS; n++){
        auto firstLayerTxn = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(txnRoot->GetTxnPtr()->GetId(), n)}));
        orphanTxns->addTxn(firstLayerTxn);
        auto secondLayerTxn = CreateOrphanTxn(TxSource::p2p, CreateTxnInputs({COutPoint(firstLayerTxn->GetTxnPtr()->GetId(), 0)}));
        orphanTxns->addTxn(secondLayerTxn);
    }
     
    BOOST_CHECK(orphanTxns->getTxnsNumber() == 2 * NUM_FIRST_LAYER_TXS);
    // release and collect all outpoints from root txn
    orphanTxns->collectTxnOutpoints(*(txnRoot->GetTxnPtr()));
    auto vTxnsToReprocess = orphanTxns->collectDependentTxnsForRetry();
    // We could release all transactions but we are limited to MAX_TXS_RELEASED
    // and we should always release first layer transactions
    BOOST_CHECK(vTxnsToReprocess.size() == NUM_FIRST_LAYER_TXS);
}

BOOST_AUTO_TEST_SUITE_END()
