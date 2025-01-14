/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/db/repl/oplog_applier_impl_test_fixture.h"

#include "mongo/db/catalog/document_validation.h"
#include "mongo/db/curop.h"
#include "mongo/db/db_raii.h"
#include "mongo/db/dbdirectclient.h"
#include "mongo/db/op_observer_registry.h"
#include "mongo/db/query/internal_plans.h"
#include "mongo/db/repl/drop_pending_collection_reaper.h"
#include "mongo/db/repl/oplog_applier.h"
#include "mongo/db/repl/replication_consistency_markers_mock.h"
#include "mongo/db/repl/replication_coordinator_mock.h"
#include "mongo/db/repl/replication_process.h"
#include "mongo/db/repl/replication_recovery_mock.h"
#include "mongo/db/repl/storage_interface.h"
#include "mongo/db/repl/storage_interface_impl.h"

namespace mongo {
namespace repl {

void OplogApplierImplOpObserver::onInserts(OperationContext* opCtx,
                                           const NamespaceString& nss,
                                           OptionalCollectionUUID uuid,
                                           std::vector<InsertStatement>::const_iterator begin,
                                           std::vector<InsertStatement>::const_iterator end,
                                           bool fromMigrate) {
    if (!onInsertsFn) {
        return;
    }
    std::vector<BSONObj> docs;
    for (auto it = begin; it != end; ++it) {
        const InsertStatement& insertStatement = *it;
        docs.push_back(insertStatement.doc.getOwned());
    }
    onInsertsFn(opCtx, nss, docs);
}

void OplogApplierImplOpObserver::onDelete(OperationContext* opCtx,
                                          const NamespaceString& nss,
                                          OptionalCollectionUUID uuid,
                                          StmtId stmtId,
                                          bool fromMigrate,
                                          const boost::optional<BSONObj>& deletedDoc) {
    if (!onDeleteFn) {
        return;
    }
    onDeleteFn(opCtx, nss, uuid, stmtId, fromMigrate, deletedDoc);
}

void OplogApplierImplOpObserver::onCreateCollection(OperationContext* opCtx,
                                                    Collection* coll,
                                                    const NamespaceString& collectionName,
                                                    const CollectionOptions& options,
                                                    const BSONObj& idIndex,
                                                    const OplogSlot& createOpTime) {
    if (!onCreateCollectionFn) {
        return;
    }
    onCreateCollectionFn(opCtx, coll, collectionName, options, idIndex);
}

void OplogApplierImplTest::setUp() {
    ServiceContextMongoDTest::setUp();

    serviceContext = getServiceContext();
    _opCtx = cc().makeOperationContext();

    ReplicationCoordinator::set(serviceContext,
                                std::make_unique<ReplicationCoordinatorMock>(serviceContext));
    ASSERT_OK(ReplicationCoordinator::get(_opCtx.get())->setFollowerMode(MemberState::RS_PRIMARY));

    StorageInterface::set(serviceContext, std::make_unique<StorageInterfaceImpl>());

    DropPendingCollectionReaper::set(
        serviceContext, std::make_unique<DropPendingCollectionReaper>(getStorageInterface()));
    repl::setOplogCollectionName(serviceContext);
    repl::createOplog(_opCtx.get());

    _consistencyMarkers = std::make_unique<ReplicationConsistencyMarkersMock>();

    // Set up an OpObserver to track the documents OplogApplierImpl inserts.
    auto opObserver = std::make_unique<OplogApplierImplOpObserver>();
    _opObserver = opObserver.get();
    auto opObserverRegistry = dynamic_cast<OpObserverRegistry*>(serviceContext->getOpObserver());
    opObserverRegistry->addObserver(std::move(opObserver));

    // Initialize the featureCompatibilityVersion server parameter. This is necessary because this
    // test fixture does not create a featureCompatibilityVersion document from which to initialize
    // the server parameter.
    serverGlobalParams.featureCompatibility.setVersion(
        ServerGlobalParams::FeatureCompatibility::Version::kFullyUpgradedTo44);
}

void OplogApplierImplTest::tearDown() {
    _opCtx.reset();
    _consistencyMarkers = {};
    DropPendingCollectionReaper::set(serviceContext, {});
    StorageInterface::set(serviceContext, {});
    ServiceContextMongoDTest::tearDown();
}

ReplicationConsistencyMarkers* OplogApplierImplTest::getConsistencyMarkers() const {
    return _consistencyMarkers.get();
}

StorageInterface* OplogApplierImplTest::getStorageInterface() const {
    return StorageInterface::get(serviceContext);
}

}  // namespace repl
}  // namespace mongo
