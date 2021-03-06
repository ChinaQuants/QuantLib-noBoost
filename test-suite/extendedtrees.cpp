/* -*- mode: c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*
 Copyright (C) 2003, 2007 Ferdinando Ametrano
 Copyright (C) 2003, 2007, 2008 StatPro Italia srl

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it
 under the terms of the QuantLib license.  You should have received a
 copy of the license along with this program; if not, please email
 <quantlib-dev@lists.sf.net>. The license is also available online at
 <http://quantlib.org/license.shtml>.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include "utilities.hpp"
#include <ql/time/daycounters/actual360.hpp>
#include <ql/instruments/europeanoption.hpp>
#include <ql/pricingengines/vanilla/analyticeuropeanengine.hpp>
#include <ql/pricingengines/vanilla/binomialengine.hpp>
#include <ql/experimental/lattices/extendedbinomialtree.hpp>
#include <ql/termstructures/yield/flatforward.hpp>
#include <ql/termstructures/volatility/equityfx/blackconstantvol.hpp>
#include <ql/utilities/dataformatters.hpp>
#include <map>

using namespace QuantLib;


#define REPORT_FAILURE(greekName, payoff, exercise, s, q, r, today, \
                       v, expected, calculated, error, tolerance) \
    FAIL_CHECK(exerciseTypeToString(exercise) << " " \
               << payoff->optionType() << " option with " \
               << payoffTypeToString(payoff) << " payoff:\n" \
               << "    spot value:       " << s << "\n" \
               << "    strike:           " << payoff->strike() << "\n" \
               << "    dividend yield:   " << io::rate(q) << "\n" \
               << "    risk-free rate:   " << io::rate(r) << "\n" \
               << "    reference date:   " << today << "\n" \
               << "    maturity:         " << exercise->lastDate() << "\n" \
               << "    volatility:       " << io::volatility(v) << "\n\n" \
               << "    expected " << greekName << ":   " << expected << "\n" \
               << "    calculated " << greekName << ": " << calculated << "\n"\
               << "    error:            " << error << "\n" \
               << "    tolerance:        " << tolerance);

namespace {

    // utilities

    enum EngineType { Analytic,
                      JR, CRR, EQP, TGEO, TIAN, LR, JOSHI };

    std::shared_ptr<GeneralizedBlackScholesProcess>
    makeProcess(const std::shared_ptr<Quote>& u,
                const std::shared_ptr<YieldTermStructure>& q,
                const std::shared_ptr<YieldTermStructure>& r,
                const std::shared_ptr<BlackVolTermStructure>& vol) {
        return std::shared_ptr<BlackScholesMertonProcess>(
           new BlackScholesMertonProcess(Handle<Quote>(u),
                                         Handle<YieldTermStructure>(q),
                                         Handle<YieldTermStructure>(r),
                                         Handle<BlackVolTermStructure>(vol)));
    }

    std::shared_ptr<VanillaOption>
    makeOption(const std::shared_ptr<StrikedTypePayoff>& payoff,
               const std::shared_ptr<Exercise>& exercise,
               const std::shared_ptr<Quote>& u,
               const std::shared_ptr<YieldTermStructure>& q,
               const std::shared_ptr<YieldTermStructure>& r,
               const std::shared_ptr<BlackVolTermStructure>& vol,
               EngineType engineType,
               Size binomialSteps) {

        std::shared_ptr<GeneralizedBlackScholesProcess> stochProcess =
            makeProcess(u,q,r,vol);

        std::shared_ptr<PricingEngine> engine;
        switch (engineType) {
          case Analytic:
            engine = std::shared_ptr<PricingEngine>(
                                    new AnalyticEuropeanEngine(stochProcess));
            break;
          case JR:
            engine = std::shared_ptr<PricingEngine>(
                new BinomialVanillaEngine<ExtendedJarrowRudd>(stochProcess,
                                                              binomialSteps));
            break;
          case CRR:
            engine = std::shared_ptr<PricingEngine>(
                new BinomialVanillaEngine<ExtendedCoxRossRubinstein>(
                                                              stochProcess,
                                                              binomialSteps));
            break;
          case EQP:
            engine = std::shared_ptr<PricingEngine>(
                new BinomialVanillaEngine<ExtendedAdditiveEQPBinomialTree>(
                                                              stochProcess,
                                                              binomialSteps));
            break;
          case TGEO:
            engine = std::shared_ptr<PricingEngine>(
                new BinomialVanillaEngine<ExtendedTrigeorgis>(stochProcess,
                                                              binomialSteps));
            break;
          case TIAN:
            engine = std::shared_ptr<PricingEngine>(
                new BinomialVanillaEngine<ExtendedTian>(stochProcess,
                                                        binomialSteps));
            break;
          case LR:
            engine = std::shared_ptr<PricingEngine>(
                      new BinomialVanillaEngine<ExtendedLeisenReimer>(
                                                              stochProcess,
                                                              binomialSteps));
            break;
          case JOSHI:
            engine = std::shared_ptr<PricingEngine>(
                new BinomialVanillaEngine<ExtendedJoshi4>(stochProcess,
                                                          binomialSteps));
            break;
          default:
            QL_FAIL("unknown engine type");
        }

        std::shared_ptr<VanillaOption> option(
                                        new EuropeanOption(payoff, exercise));
        option->setPricingEngine(engine);
        return option;
    }

}

namespace {

    void testEngineConsistency(EngineType engine,
                               Size binomialSteps,
                               std::map<std::string,Real> tolerance) {

        QL_TEST_START_TIMING

            std::map<std::string,Real> calculated, expected;

        // test options
        Option::Type types[] = { Option::Call, Option::Put };
        Real strikes[] = { 75.0, 100.0, 125.0 };
        Integer lengths[] = { 1 };

        // test data
        Real underlyings[] = { 100.0 };
        Rate qRates[] = { 0.00, 0.05 };
        Rate rRates[] = { 0.01, 0.05, 0.15 };
        Volatility vols[] = { 0.11, 0.50, 1.20 };

        DayCounter dc = Actual360();
        Date today = Date::todaysDate();

        std::shared_ptr<SimpleQuote> spot(new SimpleQuote(0.0));
        std::shared_ptr<SimpleQuote> vol(new SimpleQuote(0.0));
        std::shared_ptr<BlackVolTermStructure> volTS = flatVol(today,vol,dc);
        std::shared_ptr<SimpleQuote> qRate(new SimpleQuote(0.0));
        std::shared_ptr<YieldTermStructure> qTS = flatRate(today,qRate,dc);
        std::shared_ptr<SimpleQuote> rRate(new SimpleQuote(0.0));
        std::shared_ptr<YieldTermStructure> rTS = flatRate(today,rRate,dc);

        for (Size i=0; i<LENGTH(types); i++) {
          for (Size j=0; j<LENGTH(strikes); j++) {
            for (Size k=0; k<LENGTH(lengths); k++) {
              Date exDate = today + lengths[k]*360;
              std::shared_ptr<Exercise> exercise(
                                                new EuropeanExercise(exDate));
              std::shared_ptr<StrikedTypePayoff> payoff(new
                                    PlainVanillaPayoff(types[i], strikes[j]));
              // reference option
              std::shared_ptr<VanillaOption> refOption =
                  makeOption(payoff, exercise, spot, qTS, rTS, volTS,
                             Analytic, Null<Size>());
              // option to check
              std::shared_ptr<VanillaOption> option =
                  makeOption(payoff, exercise, spot, qTS, rTS, volTS,
                             engine, binomialSteps);

              for (Size l=0; l<LENGTH(underlyings); l++) {
                for (Size m=0; m<LENGTH(qRates); m++) {
                  for (Size n=0; n<LENGTH(rRates); n++) {
                    for (Size p=0; p<LENGTH(vols); p++) {
                      Real u = underlyings[l];
                      Rate q = qRates[m],
                           r = rRates[n];
                      Volatility v = vols[p];
                      spot->setValue(u);
                      qRate->setValue(q);
                      rRate->setValue(r);
                      vol->setValue(v);

                      expected.clear();
                      calculated.clear();

                      // FLOATING_POINT_EXCEPTION
                      expected["value"] = refOption->NPV();
                      calculated["value"] = option->NPV();

                      if (option->NPV() > spot->value()*1.0e-5) {
                           expected["delta"] = refOption->delta();
                           expected["gamma"] = refOption->gamma();
                           expected["theta"] = refOption->theta();
                           calculated["delta"] = option->delta();
                           calculated["gamma"] = option->gamma();
                           calculated["theta"] = option->theta();
                      }
                      std::map<std::string,Real>::iterator it;
                      for (it = calculated.begin();
                           it != calculated.end(); ++it) {
                          std::string greek = it->first;
                          Real expct = expected  [greek],
                               calcl = calculated[greek],
                               tol   = tolerance [greek];
                          Real error = relativeError(expct,calcl,u);
                          if (error > tol) {
                              REPORT_FAILURE(greek, payoff, exercise,
                                             u, q, r, today, v,
                                             expct, calcl, error, tol);
                          }
                      }
                    }
                  }
                }
              }
            }
          }
        }
    }

}


TEST_CASE("ExtendedTrees_JRBinomialEngines", "[ExtendedTrees]") {

    INFO("Testing time-dependent JR binomial European engines "
                       "against analytic results...");

    SavedSettings backup;

    EngineType engine = JR;
    Size steps = 251;
    std::map<std::string,Real> relativeTol;
    relativeTol["value"] = 0.002;
    relativeTol["delta"] = 1.0e-3;
    relativeTol["gamma"] = 1.0e-4;
    relativeTol["theta"] = 0.03;
    testEngineConsistency(engine, steps, relativeTol);
}

TEST_CASE("ExtendedTrees_CRRBinomialEngines", "[ExtendedTrees]") {

    INFO("Testing time-dependent CRR binomial European engines "
                       "against analytic results...");

    SavedSettings backup;

    EngineType engine = CRR;
    Size steps = 501;
    std::map<std::string,Real> relativeTol;
    relativeTol["value"] = 0.02;
    relativeTol["delta"] = 1.0e-3;
    relativeTol["gamma"] = 1.0e-4;
    relativeTol["theta"] = 0.03;
    testEngineConsistency(engine, steps, relativeTol);
}

TEST_CASE("ExtendedTrees_EQPBinomialEngines", "[ExtendedTrees]") {

    INFO("Testing time-dependent EQP binomial European engines "
                       "against analytic results...");

    SavedSettings backup;

    EngineType engine = EQP;
    Size steps = 501;
    std::map<std::string,Real> relativeTol;
    relativeTol["value"] = 0.02;
    relativeTol["delta"] = 1.0e-3;
    relativeTol["gamma"] = 1.0e-4;
    relativeTol["theta"] = 0.03;
    testEngineConsistency(engine, steps, relativeTol);
}

TEST_CASE("ExtendedTrees_TGEOBinomialEngines", "[ExtendedTrees]") {

    INFO("Testing time-dependent TGEO binomial European engines "
                       "against analytic results...");

    SavedSettings backup;

    EngineType engine = TGEO;
    Size steps = 251;
    std::map<std::string,Real> relativeTol;
    relativeTol["value"] = 0.002;
    relativeTol["delta"] = 1.0e-3;
    relativeTol["gamma"] = 1.0e-4;
    relativeTol["theta"] = 0.03;
    testEngineConsistency(engine, steps, relativeTol);
}

TEST_CASE("ExtendedTrees_TIANBinomialEngines", "[ExtendedTrees]") {

    INFO("Testing time-dependent TIAN binomial European engines "
                       "against analytic results...");

    SavedSettings backup;

    EngineType engine = TIAN;
    Size steps = 251;
    std::map<std::string,Real> relativeTol;
    relativeTol["value"] = 0.002;
    relativeTol["delta"] = 1.0e-3;
    relativeTol["gamma"] = 1.0e-4;
    relativeTol["theta"] = 0.03;
    testEngineConsistency(engine, steps, relativeTol);
}

TEST_CASE("ExtendedTrees_LRBinomialEngines", "[ExtendedTrees]") {

    INFO("Testing time-dependent LR binomial European engines "
                       "against analytic results...");

    SavedSettings backup;

    EngineType engine = LR;
    Size steps = 251;
    std::map<std::string,Real> relativeTol;
    relativeTol["value"] = 1.0e-6;
    relativeTol["delta"] = 1.0e-3;
    relativeTol["gamma"] = 1.0e-4;
    relativeTol["theta"] = 0.03;
    testEngineConsistency(engine, steps, relativeTol);
}

TEST_CASE("ExtendedTrees_JOSHIBinomialEngines", "[ExtendedTrees]") {

    INFO("Testing time-dependent Joshi binomial European engines "
                       "against analytic results...");

    SavedSettings backup;

    EngineType engine = JOSHI;
    Size steps = 251;
    std::map<std::string,Real> relativeTol;
    relativeTol["value"] = 1.0e-7;
    relativeTol["delta"] = 1.0e-3;
    relativeTol["gamma"] = 1.0e-4;
    relativeTol["theta"] = 0.03;
    testEngineConsistency(engine, steps, relativeTol);
}
