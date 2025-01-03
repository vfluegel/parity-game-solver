#include <iostream>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <unordered_set>
#include <queue>
#include <cassert>
#include <chrono>
#include <thread>
#include <future>
#include <oxidd/util.hpp>
#include <oxidd/bdd.hpp>

extern "C" {
    #include "simplehoa.h"
}

oxidd::bdd_manager manager{4194304, 1048576, 8};
std::unordered_map<int, oxidd::bdd_function> aps{};
std::unordered_map<std::string, BTree*> aliases;

struct PGTransition
{
    oxidd::bdd_function label;
    int target{ -1 };
    std::unordered_set<int> notControllable;
    int transitionPrio{ -1 };
};

struct PGState 
{
    int id;
    bool belongsToEven{ false };
};

std::vector<PGState> PGstates{};
std::vector<std::vector<int>> outgoing{};
std::vector<std::vector<int>> incoming{};
std::map<int, std::vector<std::pair<int, int>>, std::greater<int>> priorities{};

PGTransition btreeToBDD(BTree* tree, std::vector<bool>& controllable) {
    PGTransition transition{};
    if(tree == NULL) {
        return transition;
    } 
    PGTransition left;
    PGTransition right;
    switch (tree->type)
    {
    case NT_AP:
        transition.label = aps[tree->id];
        if (!controllable[tree->id]) {
            transition.notControllable.insert(tree->id);
        }
        break;
    case NT_ALIAS:
        transition = btreeToBDD(aliases[tree->alias], controllable);
        break;
    case NT_AND:
        
        left = btreeToBDD(tree->left, controllable);
        right = btreeToBDD(tree->right, controllable);
        transition.label = left.label & right.label;
        transition.notControllable.merge(left.notControllable);
        transition.notControllable.merge(right.notControllable);
        break;
    case NT_OR:
        left = btreeToBDD(tree->left, controllable);
        right = btreeToBDD(tree->right, controllable);      
        transition.label = left.label | right.label;
        transition.notControllable.merge(left.notControllable);
        transition.notControllable.merge(right.notControllable);
        break;
    case NT_NOT:
        left = btreeToBDD(tree->left, controllable);
        transition.label = ~left.label;
        transition.notControllable.merge(left.notControllable);
        break;
    case NT_BOOL:
        transition.label = tree->id ? manager.t() : manager.f();
        break;
    default:
        std::cout << "Found something without match in the tree" << std::endl;
        break;
    }

    return transition;
}

void handlePriorityTree(BTree* tree, std::vector<int>& prios)
{
    if (tree->type == NT_INF) {
        int priority = 2 * (tree->left->id + 1);
        if (prios[tree->left->id] < priority) {
            prios[tree->left->id] = priority;
        }
    } else if (tree->type == NT_FIN) {
        int priority = 2 * (tree->left->id + 1) + 1;
        if (prios[tree->left->id] < priority) {
            prios[tree->left->id] = priority;
        }
    } else {
        if (tree->left != nullptr) handlePriorityTree(tree->left, prios);
        if (tree->right != nullptr) handlePriorityTree(tree->right, prios);
    }
}

std::unordered_set<int> getAttractors(const std::unordered_set<int>& targetSet, bool evenPlayer, const std::vector<bool>& includedStates) {
    std::unordered_set<int> attractors;
    std::queue<int> processingQueue;

    // Initialize attractors with the target set
    for (int state : targetSet) {
        if (includedStates[state]) {
            attractors.insert(state);
            // Add all incoming states to the target set to be checked as attractor
            for (const auto& from : incoming[state]) {
                if (includedStates[from]) processingQueue.push(from);
            }
        }
    }

    while (!processingQueue.empty()) 
    {
        int current = processingQueue.front();
        processingQueue.pop();

        // If the state is already in the attractors, we don't have to check it again
        if (attractors.contains(current) ) {
            continue;
        }

        bool isAttractor = false;

        if (PGstates[current].belongsToEven == evenPlayer) {
            // Current player's state: add if at least one successor is in attractors
            isAttractor = true;
        } else {
            // Opponent's state: add if all successors are in attractors
            isAttractor = std::all_of(outgoing[current].begin(), outgoing[current].end(), 
                            [&](int to) { return !includedStates[to] || attractors.contains(to); });
        }    

        if (isAttractor) {
            attractors.insert(current);
            for (auto& from : incoming[current]) {
                if (includedStates[from] && !attractors.contains(from)) processingQueue.push(from);
            }
        }

    }

    return attractors;
}

std::array<std::unordered_set<int>, 2> zielonka(std::vector<bool> includedStates) {
    //std::cout << "New Zielonka iteration...\n";
    std::array<std::unordered_set<int>, 2> res{};
    if (std::all_of(includedStates.begin(), includedStates.end(), [](bool v) { return !v; })) {
        // No state is included, we can return the default constructed empty lists
        //std::cout << "No more states included, we return empty\n";
        return res;
    }
    
    int maxPrio;
    bool foundPrio = false;
    // Get the largest priority
    auto prioIterator = priorities.begin();
    while (!foundPrio)
    {
        if (prioIterator == priorities.end()) {
            // We couldn't find a priority to use - essentially the same as having all states gone
            return res;
        }
        maxPrio = prioIterator->first;
        // We can't use that priority if no state with the priority is included anymore
        foundPrio = std::any_of(priorities[maxPrio].begin(), priorities[maxPrio].end(), 
                                [&includedStates](std::pair<int, int> v) { return includedStates[v.first] && includedStates[v.second]; });
        if (!foundPrio) prioIterator++;
    }

    int i = maxPrio % 2;
    // Iterate through the transitions with the highest priority and add their endpoints to the target set
    std::unordered_set<int> prioTransitionsTargetStates;
    for (auto transition : priorities[maxPrio])
    {
        if (includedStates[transition.first] && includedStates[transition.second]) {
            // We can take the transition with the high priority from the start of the state
            prioTransitionsTargetStates.insert(transition.first);
        }
        
    }
    
    std::unordered_set<int> R = getAttractors(prioTransitionsTargetStates, i == 0, includedStates);
    // Remove all attractors from the included states
    std::vector<bool> newIncludedStates{ includedStates };
    for (int a : R)
    {
        newIncludedStates[a] = false;
    }
    auto zielonkaRes = zielonka(newIncludedStates);
    if (zielonkaRes[1-i].empty()) 
    {
        R.merge(zielonkaRes[i]);
        // Only set the first part of the pair
        res[i] = R;
    }
    else 
    {
        std::unordered_set<int> S = getAttractors(zielonkaRes[1-i], i != 0, includedStates);
        newIncludedStates = includedStates;
        for (int a : S)
        {
            newIncludedStates[a] = false;
        }
        auto zielonkaResI = zielonka(newIncludedStates);
        S.merge(zielonkaResI[1-i]);
        res[i] = zielonkaResI[i];
        res[1 - i] = S;
    }
    return res;
}

void timer(std::future<void> future, int timeout)
{
    if (future.wait_for(std::chrono::seconds(timeout)) == std::future_status::timeout) {
        std::cout << "Operation timed out!" << std::endl;
        abort(); // Timeout occurs, terminate the program
    }
}

int main(int argc, char** argv)
{
    std::cout << argv[1] << std::endl;

    std::promise<void> cancelPromise;
    std::future<void> cancelFuture = cancelPromise.get_future();

    int timeout{ argc == 3 ? std::stoi(argv[2]) : 150 };
    std::thread timerThread(timer, std::move(cancelFuture), timeout);

    try {
        HoaData hoa;
        defaultsHoa(&hoa);
        auto inputFile = fopen(argv[1], "r");
        int res = parseHoa(inputFile, &hoa);
        fclose(inputFile);
        if (res != 0) {
            std::cerr << "Input could not be parsed!\n";
            // Signal the timer thread to stop
            cancelPromise.set_value();
            // Join the timer thread to ensure proper cleanup
            timerThread.join();
            exit(1);
        }
    #ifndef NDEBUG
        generateDotFile(&hoa, "inputHOA.dot");
    #endif
        for (size_t ap = 0; ap < hoa.noAPs; ap++)
        {
            aps[ap] = manager.new_var();
        }

        for (size_t a = 0; a < hoa.noAliases; a++)
        {
            aliases[hoa.aliases[a].alias] = hoa.aliases[a].labelExpr;
        }

        std::vector<bool> controllableAPs (hoa.noAPs, false);
        for (size_t c = 0; c < hoa.noCntAPs; c++)
        {
            controllableAPs[hoa.cntAPs[c]] = true;
        }
        
        std::vector<int> parityPriorities(hoa.noAccSets, 0);
        handlePriorityTree(hoa.acc, parityPriorities);
        
        PGstates.resize(hoa.noStates);
        outgoing.resize(hoa.noStates);
        incoming.resize(hoa.noStates);
        for (size_t s = 0; s < hoa.noStates; s++)
        {
            State state = hoa.states[s];
            PGstates[s].id = state.id;
            int statePrio{ -1 };
            for (size_t a = 0; a < state.noAccSig; a++)
            {
                statePrio = std::max(statePrio, parityPriorities[state.accSig[a]]);
            }
            
            std::unordered_set<int> allUncontrollableAP;
            std::vector<PGTransition> allOutgoing;
            // Collect all outgoing transitions and not controllable APs
            for (size_t t = 0; t < state.noTrans; t++)
            {
                Transition trans = state.transitions[t];
                assert(trans.noSucc == 1);
                auto transition = btreeToBDD(trans.label, controllableAPs);
                transition.target = trans.successors[0];
                transition.transitionPrio = statePrio;

                for (size_t a = 0; a < trans.noAccSig; a++)
                {
                    transition.transitionPrio = std::max(transition.transitionPrio, parityPriorities[trans.accSig[a]]);
                }

                allOutgoing.push_back(transition);

                allUncontrollableAP.merge(transition.notControllable);
            }
            
            if (allUncontrollableAP.empty()) 
            {
                // The transitions are all entirely made up of controllable APs, we can just use it as is
                PGstates[s].belongsToEven = true;
                outgoing[state.id].reserve(allOutgoing.size());
                for (PGTransition t : allOutgoing) {
                    assert (t.target != -1);
                    if (t.transitionPrio == -1) t.transitionPrio = parityPriorities.back() + 1;
                    priorities[t.transitionPrio].emplace_back(state.id, t.target);
                    outgoing[state.id].push_back(t.target);
                    incoming[t.target].push_back(state.id);
                }
            }
            else 
            {
                int numNotControllable = allUncontrollableAP.size();
                // Possible combinations for the uncontrollable vars
                int numCombinations = 1 << numNotControllable; 
                
                outgoing[state.id].reserve(numCombinations);
                for (int comb = 0; comb < numCombinations; comb++) {
                    // Start by fixing the first variable in the uncontrollable set
                    bool val = (comb & (1 << 0)) != 0;
                    auto uncontrollableAPIterator = allUncontrollableAP.begin();
                    oxidd::bdd_function fixedTransition = val ? aps[*uncontrollableAPIterator] : ~aps[*uncontrollableAPIterator];
                    uncontrollableAPIterator++;

                    // Now go over the rest of the uncontrollable vars and fix those the same way
                    for (int v = 1; v < numNotControllable; v++) {
                        assert(uncontrollableAPIterator != allUncontrollableAP.end());
                        val = (comb & (1 << v)) != 0;
                        fixedTransition &= val ? aps[*uncontrollableAPIterator] : ~aps[*uncontrollableAPIterator];
                        uncontrollableAPIterator++;
                    }

                    size_t intermediaryState = PGstates.size();
                    PGstates.push_back({state.id + hoa.noStates + comb, true});
                    outgoing[state.id].push_back(intermediaryState);
                    incoming.push_back({state.id});
                    if (incoming.size() - 1 != intermediaryState) std::cerr << "The last element of incoming does not match the newTransition\n";
                    
                    outgoing.emplace_back();
                    if (outgoing.size() - 1 != intermediaryState) std::cerr << "The last element of outgoing does not match the newTransition\n";
                    outgoing[intermediaryState].reserve(allOutgoing.size());
                    for (PGTransition t : allOutgoing) {
                        oxidd::bdd_function newLabel = fixedTransition & t.label;
                        if (newLabel.satisfiable()) {
                            assert (t.target != -1);
                            if (t.transitionPrio == -1) t.transitionPrio = parityPriorities.back() + 1;
                            priorities[t.transitionPrio].emplace_back(intermediaryState, t.target);
                            outgoing[intermediaryState].push_back(t.target);
                            incoming[t.target].push_back(intermediaryState);
                        }   
                    }
                }
            }
            
        }

        std::vector<bool> included (PGstates.size(), true);
        auto [w_even, w_odd] = zielonka(included);
    #ifndef NDEBUG
        std::cout << "Even player winning positions: " << w_even.size() << std::endl;
        std::cout << "Odd player winning positions: " << w_odd.size() << std::endl;
        assert(w_even.size() + w_odd.size() == PGstates.size());
    #endif    
        bool evenWins = true;
        for (int s = 0; s < hoa.noStart; s++)
        {
            if (!w_even.contains(hoa.start[s])) 
            {
                if (s > 0) std::cout << "Starting states in both player's regions..." << std::endl;
                assert (w_odd.contains(hoa.start[s]));
                evenWins = false;
                break;
            }
        }
        std::cout << "Realizable: " << evenWins << std::endl;
    }
    catch (std::length_error& e)
    {
        std::cout << "Not enough memory" << std::endl;
    }
    
    // Signal the timer thread to stop
    cancelPromise.set_value();
    // Join the timer thread to ensure proper cleanup
    timerThread.join();

}
