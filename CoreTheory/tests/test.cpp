#include "gtest/gtest.h"

import CoreTheory;
using namespace core_theory::state_machine;

TEST(DFA, positive)
{
    Fsm<uint8_t> fsm{"DFA_pos"};
    StateId s0 = fsm.addState("0", false);
    StateId s1 = fsm.addState("1", false);
    StateId s2 = fsm.addState("2", false);
    StateId s3 = fsm.addState("3", true);
    fsm.setStartState(s0);

    fsm.addTransition(s0, s1, 1);
    fsm.addTransition(s0, s2, 0);

    fsm.addTransition(s1, s0, 1);
    fsm.addTransition(s1, s1, 0);

    fsm.addTransition(s2, s3, 1);
    fsm.addTransition(s2, s0, 0);

    fsm.addTransition(s3, s3, 1);
    fsm.addTransition(s3, s2, 0);

    FsmRunner fsmRunner{fsm};

    std::vector<uint8_t> input{1, 0, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < input.size() - 1; i++)
    {
        fsmRunner.step(input[i]);
        ASSERT_FALSE(fsmRunner.isAccepted());
    }
    fsmRunner.step(input[input.size() - 1]);
    ASSERT_TRUE(fsmRunner.isAccepted());
}

TEST(DFA, negative)
{
    Fsm<uint8_t> fsm{"DFA_pos"};
    StateId s0 = fsm.addState("0", false);
    StateId s1 = fsm.addState("1", false);
    StateId s2 = fsm.addState("2", false);
    StateId s3 = fsm.addState("3", true);
    fsm.setStartState(s0);

    fsm.addTransition(s0, s1, 1);
    fsm.addTransition(s0, s2, 0);

    fsm.addTransition(s1, s0, 1);
    fsm.addTransition(s1, s1, 0);

    fsm.addTransition(s2, s3, 1);
    fsm.addTransition(s2, s0, 0);

    fsm.addTransition(s3, s3, 1);
    fsm.addTransition(s3, s2, 0);

    FsmRunner fsmRunner{fsm};

    std::vector<uint8_t> input{1, 0, 0, 0, 0, 1};
    for (int i = 0; i < input.size(); i++)
    {
        fsmRunner.step(input[i]);
    }
    ASSERT_FALSE(fsmRunner.isAccepted());
}

TEST(NFA, positive)
{
    Fsm<uint8_t> fsm{"DFA_pos"};
    StateId s0 = fsm.addState("0", false);
    StateId s1 = fsm.addState("1", false);
    StateId s2 = fsm.addState("2", false);
    StateId s3 = fsm.addState("3", false);
    StateId s4 = fsm.addState("4", true);
    fsm.setStartState(s0);

    fsm.addTransition(s0, s0, 1);
    fsm.addTransition(s0, s0, 0);
    fsm.addTransition(s0, s1, 1);

    fsm.addTransition(s1, s2, 0);

    fsm.addTransition(s2, s3, 1);

    fsm.addTransition(s3, s4, 0);

    fsm.addTransition(s4, s4, 1);
    fsm.addTransition(s4, s4, 0);

    FsmRunner fsmRunner{fsm};

    std::vector<uint8_t> input{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 1, 1};
    for (uint8_t i : input)
    {
        fsmRunner.step(i);
    }
    ASSERT_TRUE(fsmRunner.isAccepted());
}

TEST(NFA, negative)
{
    Fsm<uint8_t> fsm{"DFA_pos"};
    StateId s0 = fsm.addState("0", false);
    StateId s1 = fsm.addState("1", false);
    StateId s2 = fsm.addState("2", false);
    StateId s3 = fsm.addState("3", false);
    StateId s4 = fsm.addState("4", true);
    fsm.setStartState(s0);

    fsm.addTransition(s0, s0, 1);
    fsm.addTransition(s0, s0, 0);
    fsm.addTransition(s0, s1, 1);

    fsm.addTransition(s1, s2, 0);

    fsm.addTransition(s2, s3, 1);

    fsm.addTransition(s3, s4, 0);

    fsm.addTransition(s4, s4, 1);
    fsm.addTransition(s4, s4, 0);

    FsmRunner fsmRunner{fsm};

    std::vector<uint8_t> input{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1};
    for (uint8_t i : input)
    {
        fsmRunner.step(i);
    }
    ASSERT_FALSE(fsmRunner.isAccepted());
}

TEST(EpsilonClosure, positive)
{
    Fsm<uint8_t> fsm{"DFA_pos"};
    StateId s0 = fsm.addState("0", false);
    StateId s1 = fsm.addState("1", false);
    StateId s2 = fsm.addState("2", false);
    StateId s3 = fsm.addState("3", true);
    fsm.setStartState(s0);

    fsm.addTransition(s0, s1, 0);

    fsm.addTransition(s1, s0, 1);

    fsm.addTransition(s1, s2);

    fsm.addTransition(s2, s3, 0);

    fsm.addTransition(s3, s3, 1);

    FsmRunner fsmRunner{fsm};

    fsmRunner.step(0);
    fsmRunner.step(0);

    ASSERT_TRUE(fsmRunner.isAccepted());
}

TEST(EpsilonClosure, negative)
{
    Fsm<uint8_t> fsm{"DFA_pos"};
    StateId s0 = fsm.addState("0", false);
    StateId s1 = fsm.addState("1", false);
    StateId s2 = fsm.addState("2", false);
    StateId s3 = fsm.addState("3", true);
    fsm.setStartState(s0);

    fsm.addTransition(s0, s1, 0);

    fsm.addTransition(s1, s0, 1);

    fsm.addTransition(s1, s2);

    fsm.addTransition(s2, s3, 0);

    fsm.addTransition(s3, s3, 1);

    FsmRunner fsmRunner{fsm};

    fsmRunner.step(0);
    fsmRunner.step(1);

    ASSERT_FALSE(fsmRunner.isAccepted());
}
