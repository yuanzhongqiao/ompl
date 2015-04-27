/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2014, University of Toronto
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
*   * Neither the name of the University of Toronto nor the names of its
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

/* Authors: Jonathan Gammell */

#ifndef OMPL_BASE_SAMPLERS_INFORMED_GENERAL_REJECTION_INFORMED_SAMPLER_
#define OMPL_BASE_SAMPLERS_INFORMED_GENERAL_REJECTION_INFORMED_SAMPLER_

// We inherit from InformedStateSampler
#include "ompl/base/samplers/InformedStateSampler.h"

namespace ompl
{
    namespace base
    {
        /** \brief A default rejection sampling scheme that samples uniformly from the entire planning domain.
        Samples are rejected until one is found that has a heuristic solution estimate that is less than the current solution.
        In general, direct sampling of the informed subset is much better, but this is a general default.

        */
        class RejectionInfSampler : public InformedStateSampler
        {
        public:
            /** \brief Construct a rejection sampler that only generates states with a heuristic solution estimate that is less than the cost of the current solution. */
            RejectionInfSampler(const StateSpace* space, const ProblemDefinitionPtr probDefn, const Cost* bestCost);
            virtual ~RejectionInfSampler()
            {
            }

            /** \brief Sample uniformly in the subset of the state space whose heuristic solution estimates are less than the provided cost. */
            void sampleUniform(State* statePtr, const Cost& maxCost);

            /** \brief Sample uniformly in the subset of the state space whose heuristic solution estimates are between the provided costs. */
            void sampleUniform(State* statePtr, const Cost& minCost, const Cost& maxCost);

            /** \brief Whether the sampler can provide a measure of the informed subset */
            bool hasInformedMeasure() const;

            /** \brief The measure of the subset of the state space defined by the current solution cost that is being searched. As rejection sampling has no closed-form knowledge of the informed subset, the measure of the informed space is always the measure of the entire space. */
            virtual double getInformedMeasure() const;

            /** \brief The measure of the subset of the state space defined by the current solution cost that is being searched. As rejection sampling has no closed-form knowledge of the informed subset, the measure of the informed space is always the measure of the entire space. */
            virtual double getInformedMeasure(const Cost& /*currentCost*/) const;

            /** \brief The measure of the subset of the state space defined by the current solution cost that is being searched. As rejection sampling has no closed-form knowledge of the informed subset, the measure of the informed space is always the measure of the entire space. */
            virtual double getInformedMeasure(const Cost& /*minCost*/, const Cost& /*maxCost*/) const;

        private:
            // Variables
            /** \brief The basic raw sampler used to generate samples to keep/reject. */
            StateSamplerPtr baseSampler_;

            /** \brief A helper function to compare whether cost \e c1 is worse than cost \e c2. Defined as opt_->isCostBetterThan(c2, c1), as if c2 is better than c1, then c1 is worse than c2. */
            bool isCostWorseThan(const Cost& c1, const Cost& c2) const;
        };
    }
}


#endif // OMPL_BASE_SAMPLERS_INFORMED_REJECTION_INFORMED_SAMPLER_
