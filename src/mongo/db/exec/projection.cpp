/**
 *    Copyright (C) 2013 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/db/exec/projection.h"

#include "mongo/db/diskloc.h"
#include "mongo/db/exec/plan_stage.h"
#include "mongo/db/exec/working_set_common.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/matcher/expression.h"
#include "mongo/util/mongoutils/str.h"

namespace mongo {

    ProjectionStage::ProjectionStage(BSONObj projObj,
                                     const MatchExpression* fullExpression,
                                     WorkingSet* ws,
                                     PlanStage* child)
        : _exec(new ProjectionExec(projObj, fullExpression)),
          _ws(ws),
          _child(child) { }

    ProjectionStage::~ProjectionStage() { }

    bool ProjectionStage::isEOF() { return _child->isEOF(); }

    PlanStage::StageState ProjectionStage::work(WorkingSetID* out) {
        ++_commonStats.works;

        WorkingSetID id = WorkingSet::INVALID_ID;
        StageState status = _child->work(&id);

        // Note that we don't do the normal if isEOF() return EOF thing here.  Our child might be a
        // tailable cursor and isEOF() would be true even if it had more data...
        if (PlanStage::ADVANCED == status) {
            WorkingSetMember* member = _ws->get(id);
            Status projStatus = _exec->transform(member);
            if (!projStatus.isOK()) {
                warning() << "Couldn't execute projection, status = "
                          << projStatus.toString() << endl;
                *out = WorkingSetCommon::allocateStatusMember(_ws, projStatus);
                return PlanStage::FAILURE;
            }

            *out = id;
            ++_commonStats.advanced;
        }
        else if (PlanStage::FAILURE == status) {
            *out = id;
            // If a stage fails, it may create a status WSM to indicate why it
            // failed, in which case 'id' is valid.  If ID is invalid, we
            // create our own error message.
            if (WorkingSet::INVALID_ID == id) {
                mongoutils::str::stream ss;
                ss << "projection stage failed to read in results from child";
                Status status(ErrorCodes::InternalError, ss);
                *out = WorkingSetCommon::allocateStatusMember( _ws, status);
            }
        }
        else if (PlanStage::NEED_FETCH == status) {
            *out = id;
            ++_commonStats.needFetch;
        }

        return status;
    }

    void ProjectionStage::prepareToYield() {
        ++_commonStats.yields;
        _child->prepareToYield();
    }

    void ProjectionStage::recoverFromYield() {
        ++_commonStats.unyields;
        _child->recoverFromYield();
    }

    void ProjectionStage::invalidate(const DiskLoc& dl, InvalidationType type) {
        ++_commonStats.invalidates;
        _child->invalidate(dl, type);
    }

    PlanStageStats* ProjectionStage::getStats() {
        _commonStats.isEOF = isEOF();
        auto_ptr<PlanStageStats> ret(new PlanStageStats(_commonStats, STAGE_PROJECTION));
        ret->children.push_back(_child->getStats());
        return ret.release();
    }

}  // namespace mongo
