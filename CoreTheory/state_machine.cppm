module;

#include <string>
#include <optional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

export module CoreTheory:StateMachine;

export namespace core_theory::state_machine
{
    using StateId = std::size_t;

    struct State {
        StateId id;
        std::string name;
        bool isAccepting{false};
    };

    template<typename SymbolType = char>
    class TransitionTable {
    public:
        using PredicateFn = bool (*)(SymbolType);

        struct Transition {
            StateId targetState;

            std::optional<SymbolType> symbol;
            PredicateFn predicate{nullptr};

            [[nodiscard]]
            bool isEpsilon() const {
                return !symbol.has_value() && predicate == nullptr;
            }
        };

        void addTransition(StateId from, StateId to, SymbolType symbol) {
            m_adjList[from].push_back({to, symbol, nullptr});
        }

        void addPredicateTransition(StateId from, StateId to, PredicateFn pred) {
            m_adjList[from].push_back({to, std::nullopt, pred});
        }

        void addEpsilonTransition(StateId from, StateId to) {
            m_adjList[from].push_back({to, std::nullopt, nullptr});
        }

        std::vector<StateId> getNextStates(StateId current, SymbolType input) const {
            std::vector<StateId> nextStates;
            auto it = m_adjList.find(current);
            if (it != m_adjList.end()) {
                for (const auto& trans : it->second) {
                    if (trans.predicate && trans.predicate(input)) {
                        nextStates.push_back(trans.targetState);
                    } else if (trans.symbol.has_value() && trans.symbol.value() == input) {
                        nextStates.push_back(trans.targetState);
                    }
                }
            }
            return nextStates;
        }

        std::vector<StateId> getEpsilonTransitions(StateId current) const {
            std::vector<StateId> epsStates;
            auto it = m_adjList.find(current);
            if (it != m_adjList.end()) {
                for (const auto& trans : it->second) {
                    if (trans.isEpsilon()) {
                        epsStates.push_back(trans.targetState);
                    }
                }
            }
            return epsStates;
        }

    private:
        std::unordered_map<StateId, std::vector<Transition>> m_adjList;
    };

    template<typename SymbolType = char>
    class Fsm {

        const std::string m_name;
        std::vector<State> m_states;
        StateId m_startState{0};
        TransitionTable<SymbolType> m_transitions;

    public:
        explicit Fsm(std::string name) : m_name(std::move(name)) {}

        StateId addState(const std::string& name, bool isAccepting = false) {
            StateId id = m_states.size();
            m_states.push_back({id, name, isAccepting});
            return id;
        }

        void setStartState(StateId id) { m_startState = id; }
        StateId getStartState() const { return m_startState; }

        void addTransition(StateId from, StateId to, SymbolType sym) {
            m_transitions.addTransition(from, to, sym);
        }

        void addTransition(StateId from, StateId to)
        {
            m_transitions.addEpsilonTransition(from, to);
        }

        void addPredicateTransition(StateId from, StateId to, typename TransitionTable<SymbolType>::PredicateFn pred) {
            m_transitions.addPredicateTransition(from, to, std::move(pred));
        }

        [[nodiscard]]
        bool isAccepting(const StateId id) const {
            return m_states.at(id).isAccepting;
        }

        const TransitionTable<SymbolType>& getTransitions() const { return m_transitions; }
    };

    template<typename SymbolType = char>
    class FsmRunner {
        const Fsm<SymbolType>& m_fsm;
        std::unordered_set<StateId> m_currentStates;

        std::unordered_set<StateId> computeEpsilonClosure(const std::unordered_set<StateId>& states) const {
            std::unordered_set<StateId> closure = states;
            std::queue<StateId> worklist;

            for (StateId s : states) {
                worklist.push(s);
            }

            while (!worklist.empty()) {
                StateId current = worklist.front();
                worklist.pop();

                auto epsTargets = m_fsm.getTransitions().getEpsilonTransitions(current);
                for (StateId target : epsTargets) {
                    if (closure.find(target) == closure.end()) {
                        closure.insert(target);
                        worklist.push(target);
                    }
                }
            }
            return closure;
        }

    public:
        explicit FsmRunner(const Fsm<SymbolType>& fsm) : m_fsm(fsm) {
            reset();
        }

        void reset() {
            m_currentStates.clear();
            std::unordered_set<StateId> initial = { m_fsm.getStartState() };
            m_currentStates = computeEpsilonClosure(initial);
        }

        void step(const SymbolType& input) {
            std::unordered_set<StateId> nextActiveStates;

            for (StateId state : m_currentStates) {
                auto targets = m_fsm.getTransitions().getNextStates(state, input);
                for (StateId target : targets) {
                    nextActiveStates.insert(target);
                }
            }

            m_currentStates = computeEpsilonClosure(nextActiveStates);
        }

        bool isAccepted() const {
            for (StateId state : m_currentStates) {
                if (m_fsm.isAccepting(state)) return true;
            }
            return false;
        }
    };
}