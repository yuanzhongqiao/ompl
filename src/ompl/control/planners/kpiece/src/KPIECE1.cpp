/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2010, Rice University
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Rice University nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/* Author: Ioan Sucan */

#include "ompl/control/planners/kpiece/KPIECE1.h"
#include "ompl/base/GoalSampleableRegion.h"
#include "ompl/tools/config/SelfConfig.h"
#include "ompl/util/Exception.h"
#include <limits>
#include <cassert>

ompl::control::KPIECE1::KPIECE1(const SpaceInformationPtr &si) : base::Planner(si, "KPIECE1")
{
    specs_.approximateSolutions = true;

    siC_ = si.get();
    nCloseSamples_ = 30;
    goalBias_ = 0.05;
    selectBorderFraction_ = 0.8;
    badScoreFactor_ = 0.45;
    goodScoreFactor_ = 0.9;
    tree_.grid.onCellUpdate(computeImportance, NULL);

    Planner::declareParam<double>("goal_bias", this, &KPIECE1::setGoalBias, &KPIECE1::getGoalBias);
    Planner::declareParam<double>("border_fraction", this, &KPIECE1::setBorderFraction, &KPIECE1::getBorderFraction);
}

ompl::control::KPIECE1::~KPIECE1(void)
{
    freeMemory();
}

void ompl::control::KPIECE1::setup(void)
{
    Planner::setup();
    SelfConfig sc(si_, getName());
    sc.configureProjectionEvaluator(projectionEvaluator_);

    if (badScoreFactor_ < std::numeric_limits<double>::epsilon() || badScoreFactor_ > 1.0)
        throw Exception("Bad cell score factor must be in the range (0,1]");
    if (goodScoreFactor_ < std::numeric_limits<double>::epsilon() || goodScoreFactor_ > 1.0)
        throw Exception("Good cell score factor must be in the range (0,1]");
    if (selectBorderFraction_ < std::numeric_limits<double>::epsilon() || selectBorderFraction_ > 1.0)
        throw Exception("The fraction of time spent selecting border cells must be in the range (0,1]");

    tree_.grid.setDimension(projectionEvaluator_->getDimension());
}

void ompl::control::KPIECE1::clear(void)
{
    Planner::clear();
    controlSampler_.reset();
    freeMemory();
    tree_.grid.clear();
    tree_.size = 0;
    tree_.iteration = 1;
}

void ompl::control::KPIECE1::freeMemory(void)
{
    freeGridMotions(tree_.grid);
}

void ompl::control::KPIECE1::freeGridMotions(Grid &grid)
{
    for (Grid::iterator it = grid.begin(); it != grid.end() ; ++it)
        freeCellData(it->second->data);
}

void ompl::control::KPIECE1::freeCellData(CellData *cdata)
{
    for (unsigned int i = 0 ; i < cdata->motions.size() ; ++i)
        freeMotion(cdata->motions[i]);
    delete cdata;
}

void ompl::control::KPIECE1::freeMotion(Motion *motion)
{
    if (motion->state)
        si_->freeState(motion->state);
    if (motion->control)
        siC_->freeControl(motion->control);
    delete motion;
}

bool ompl::control::KPIECE1::CloseSamples::consider(Grid::Cell *cell, Motion *motion, double distance)
{
    if (samples.empty())
    {
        CloseSample cs(cell, motion, distance);
        samples.insert(cs);
        return true;
    }
    // if the sample we're considering is closer to the goal than the worst sample in the
    // set of close samples, we include it
    if (samples.rbegin()->distance > distance)
    {
        // if the inclusion would go above the maximum allowed size,
        // remove the last element
        if (samples.size() >= maxSize)
            samples.erase(--samples.end());
        CloseSample cs(cell, motion, distance);
        samples.insert(cs);
        return true;
    }

    return false;
}

bool ompl::control::KPIECE1::CloseSamples::selectMotion(Motion* &smotion, Grid::Cell* &scell)
{
    if (samples.size() > 0)
    {
        scell = samples.begin()->cell;
        smotion = samples.begin()->motion;
        // average the highest & lowest distances and multiply by 1.1
        // (make the distance appear artificially longer)
        double d = (samples.begin()->distance + samples.rbegin()->distance) * 0.55;
        samples.erase(samples.begin());
        consider(scell, smotion, d);
        return true;
    }
    return false;
}

unsigned int ompl::control::KPIECE1::findNextMotion(const std::vector<Grid::Coord> &coords, unsigned int index, unsigned int count)
{
    for (unsigned int i = index + 1 ; i < count ; ++i)
        if (coords[i] != coords[index])
            return i - 1;

    return count - 1;
}

bool ompl::control::KPIECE1::solve(const base::PlannerTerminationCondition &ptc)
{
    checkValidity();
    base::Goal *goal = pdef_->getGoal().get();

    while (const base::State *st = pis_.nextStart())
    {
        Motion *motion = new Motion(siC_);
        si_->copyState(motion->state, st);
        siC_->nullControl(motion->control);
        addMotion(motion, 1.0);
    }

    if (tree_.grid.size() == 0)
    {
        msg_.error("There are no valid initial states!");
        return false;
    }

    if (!controlSampler_)
        controlSampler_ = siC_->allocControlSampler();

    msg_.inform("Starting with %u states", tree_.size);

    Motion *solution  = NULL;
    Motion *approxsol = NULL;
    double  approxdif = std::numeric_limits<double>::infinity();

    Control *rctrl = siC_->allocControl();

    std::vector<base::State*> states(siC_->getMaxControlDuration() + 1);
    std::vector<Grid::Coord>  coords(states.size());
    std::vector<Grid::Cell*>  cells(coords.size());

    for (unsigned int i = 0 ; i < states.size() ; ++i)
        states[i] = si_->allocState();

    // samples that were found to be the best, so far
    CloseSamples closeSamples(nCloseSamples_);

    while (ptc() == false)
    {
        tree_.iteration++;

        /* Decide on a state to expand from */
        Motion     *existing = NULL;
        Grid::Cell *ecell = NULL;

        if (closeSamples.canSample() && rng_.uniform01() < goalBias_)
        {
            if (!closeSamples.selectMotion(existing, ecell))
                selectMotion(existing, ecell);
        }
        else
            selectMotion(existing, ecell);
        assert(existing);

        /* sample a random control */
        controlSampler_->sampleNext(rctrl, existing->control, existing->state);

        /* propagate */
        unsigned int cd = controlSampler_->sampleStepCount(siC_->getMinControlDuration(), siC_->getMaxControlDuration());
        cd = siC_->propagateWhileValid(existing->state, rctrl, cd, states, false);

        /* if we have enough steps */
        if (cd >= siC_->getMinControlDuration())
        {
            std::size_t avgCov_two_thirds = (2 * tree_.size) / (3 * tree_.grid.size());
            bool interestingMotion = false;

            // split the motion into smaller ones, so we do not cross cell boundaries
            for (unsigned int i = 0 ; i < cd ; ++i)
            {
                projectionEvaluator_->computeCoordinates(states[i], coords[i]);
                cells[i] = tree_.grid.getCell(coords[i]);
                if (!cells[i])
                    interestingMotion = true;
                else
                {
                    if (!interestingMotion && cells[i]->data->motions.size() <= avgCov_two_thirds)
                        interestingMotion = true;
                }
            }

            if (interestingMotion || rng_.uniform01() < 0.05)
            {
                unsigned int index = 0;
                while (index < cd)
                {
                    unsigned int nextIndex = findNextMotion(coords, index, cd);
                    Motion *motion = new Motion(siC_);
                    si_->copyState(motion->state, states[nextIndex]);
                    siC_->copyControl(motion->control, rctrl);
                    motion->steps = nextIndex - index + 1;
                    motion->parent = existing;

                    double dist = 0.0;
                    bool solved = goal->isSatisfied(motion->state, &dist);
                    Grid::Cell *toCell = addMotion(motion, dist);

                    if (solved)
                    {
                        approxdif = dist;
                        solution = motion;
                        break;
                    }
                    if (dist < approxdif)
                    {
                        approxdif = dist;
                        approxsol = motion;
                    }

                    closeSamples.consider(toCell, motion, dist);

                    // new parent will be the newly created motion
                    existing = motion;
                    index = nextIndex + 1;
                }

                if (solution)
                    break;
            }

            // update cell score
            ecell->data->score *= goodScoreFactor_;
        }
        else
            ecell->data->score *= badScoreFactor_;

        tree_.grid.update(ecell);
    }

    bool approximate = false;
    if (solution == NULL)
    {
        solution = approxsol;
        approximate = true;
    }

    if (solution != NULL)
    {
        /* construct the solution path */
        std::vector<Motion*> mpath;
        while (solution != NULL)
        {
            mpath.push_back(solution);
            solution = solution->parent;
        }

        /* set the solution path */
        PathControl *path = new PathControl(si_);
        for (int i = mpath.size() - 1 ; i >= 0 ; --i)
        {
            path->states.push_back(si_->cloneState(mpath[i]->state));
            if (mpath[i]->parent)
            {
                path->controls.push_back(siC_->cloneControl(mpath[i]->control));
                path->controlDurations.push_back(mpath[i]->steps * siC_->getPropagationStepSize());
            }
        }

        goal->setDifference(approxdif);
        goal->setSolutionPath(base::PathPtr(path), approximate);

        if (approximate)
            msg_.warn("Found approximate solution");
    }

    siC_->freeControl(rctrl);
    for (unsigned int i = 0 ; i < states.size() ; ++i)
        si_->freeState(states[i]);

    msg_.inform("Created %u states in %u cells (%u internal + %u external)", tree_.size, tree_.grid.size(),
                 tree_.grid.countInternal(), tree_.grid.countExternal());

    return goal->isAchieved();
}

bool ompl::control::KPIECE1::selectMotion(Motion* &smotion, Grid::Cell* &scell)
{
    scell = rng_.uniform01() < std::max(selectBorderFraction_, tree_.grid.fracExternal()) ?
        tree_.grid.topExternal() : tree_.grid.topInternal();

    // We are running on finite precision, so our update scheme will end up
    // with 0 values for the score. This is where we fix the problem
    if (scell->data->score < std::numeric_limits<double>::epsilon())
    {
        msg_.debug("Numerical precision limit reached. Resetting costs.");
        std::vector<CellData*> content;
        content.reserve(tree_.grid.size());
        tree_.grid.getContent(content);
        for (std::vector<CellData*>::iterator it = content.begin() ; it != content.end() ; ++it)
            (*it)->score += 1.0 + log((double)((*it)->iteration));
        tree_.grid.updateAll();
    }

    if (scell && !scell->data->motions.empty())
    {
        scell->data->selections++;
        smotion = scell->data->motions[rng_.halfNormalInt(0, scell->data->motions.size() - 1)];
        return true;
    }
    else
        return false;
}

ompl::control::KPIECE1::Grid::Cell* ompl::control::KPIECE1::addMotion(Motion *motion, double dist)
{
    Grid::Coord coord;
    projectionEvaluator_->computeCoordinates(motion->state, coord);
    Grid::Cell* cell = tree_.grid.getCell(coord);
    if (cell)
    {
        cell->data->motions.push_back(motion);
        cell->data->coverage += motion->steps;
        tree_.grid.update(cell);
    }
    else
    {
        cell = tree_.grid.createCell(coord);
        cell->data = new CellData();
        cell->data->motions.push_back(motion);
        cell->data->coverage = motion->steps;
        cell->data->iteration = tree_.iteration;
        cell->data->selections = 1;
        cell->data->score = (1.0 + log((double)(tree_.iteration))) / (1e-3 + dist);
        tree_.grid.add(cell);
    }
    tree_.size++;
    return cell;
}

void ompl::control::KPIECE1::getPlannerData(base::PlannerData &data) const
{
    Planner::getPlannerData(data);

    Grid::CellArray cells;
    tree_.grid.getCells(cells);

    if (PlannerData *cpd = dynamic_cast<control::PlannerData*>(&data))
    {
        double delta = siC_->getPropagationStepSize();

        for (unsigned int i = 0 ; i < cells.size() ; ++i)
            for (unsigned int j = 0 ; j < cells[i]->data->motions.size() ; ++j)
            {
                const Motion* m = cells[i]->data->motions[j];
                if (m->parent)
                    cpd->recordEdge(m->parent->state, m->state, m->control, m->steps * delta);
                else
                    cpd->recordEdge(NULL, m->state, NULL, 0.);
                cpd->tagState(m->state, cells[i]->border ? 2 : 1);
            }
    }
    else
    {
        for (unsigned int i = 0 ; i < cells.size() ; ++i)
            for (unsigned int j = 0 ; j < cells[i]->data->motions.size() ; ++j)
            {
                const Motion* m = cells[i]->data->motions[j];
                data.recordEdge(m->parent ? m->parent->state : NULL, m->state);
                data.tagState(m->state, cells[i]->border ? 2 : 1);
            }
    }
}
