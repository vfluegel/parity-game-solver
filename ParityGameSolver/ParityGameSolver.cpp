#include <iostream>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <unordered_set>
#include <queue>
#include <cassert>
#include <oxidd/util.hpp>
#include <oxidd/bdd.hpp>

extern "C" {
    #include "simplehoa.h"
}

oxidd::bdd_manager manager{20480, 10240, 8};
std::unordered_map<int, oxidd::bdd_function> aps{};
std::unordered_map<std::string, BTree*> aliases;

struct PGTransition
{
    oxidd::bdd_function label;
    std::unordered_set<int> notControllable;
    int target{ -1 };
};

struct PGState 
{
    int id;
    oxidd::bdd_function notControllable;
    bool belongsToEven{ false };
};

struct ParityGame 
{
//    std::unordered_map<PGState, size_t> inverse{};
    std::vector<PGState> states{};
};

std::vector<PGState> PGstates{};// std::unordered_map<PGState, size_t> inverse{};
std::unordered_map<int, std::unordered_map<int, oxidd::bdd_function>> outgoingTransitions{};
std::unordered_map<int, std::unordered_map<int, oxidd::bdd_function>> incomingTransitions{};
std::map<int, std::vector<int>, std::greater<int>> priorities{};

PGTransition btreeToBDD(BTree* tree, std::vector<bool> controllable) {
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
        transition.notControllable.insert(left.notControllable.begin(), left.notControllable.end());
        transition.notControllable.insert(right.notControllable.begin(), right.notControllable.end());
        break;
    case NT_OR:
        left = btreeToBDD(tree->left, controllable);
        right = btreeToBDD(tree->right, controllable);      
        transition.label = left.label | right.label;
        transition.notControllable.insert(left.notControllable.begin(), left.notControllable.end());
        transition.notControllable.insert(right.notControllable.begin(), right.notControllable.end());
        break;
    case NT_NOT:
        left = btreeToBDD(tree->left, controllable);
        transition.label = ~left.label;
        transition.notControllable.insert(left.notControllable.begin(), left.notControllable.end());
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

std::unordered_set<int> getAttractors(const std::vector<int>& targetSet, bool evenPlayer, const std::vector<bool>& includedStates) {
    std::unordered_set<int> attractors;
    std::queue<int> processingQueue;

    // Initialize attractors with the target set
    for (int state : targetSet) {
        if (includedStates[state]) {
            processingQueue.push(state);
            attractors.insert(state);
        }
    }

    while (!processingQueue.empty()) 
    {
        int current = processingQueue.front();
        processingQueue.pop();

        for (const auto& [from, label] : incomingTransitions[current]) {
            if (!includedStates[from] || !label.satisfiable()) {
                continue;
            }

            bool isAttractor = false;

            if (PGstates[from].belongsToEven == evenPlayer) {
                // Current player's state: add if at least one successor is in attractors
                isAttractor = true;
            } else {
                // Opponent's state: add if all successors are in attractors
                isAttractor = true;
                for (const auto& [to, outLabel] : outgoingTransitions[from]) {
                    if (includedStates[to] && outLabel.satisfiable() && !attractors.contains(to)) {
                        isAttractor = false;
                        break;
                    }
                }
            }

            if (isAttractor && !attractors.contains(from)) {
                assert (includedStates[from]);
                attractors.insert(from);
                processingQueue.push(from);
            }
        }
    }

    return attractors;
}

std::array<std::unordered_set<int>, 2> zielonka(std::vector<bool> includedStates) {
    std::cout << "New Zielonka iteration...\n";
    std::array<std::unordered_set<int>, 2> res{};
    if (std::all_of(includedStates.begin(), includedStates.end(), [](bool v) { return !v; })) {
        // No state is included, we can return the default constructed empty lists
        std::cout << "No more states included, we return empty\n";
        return res;
    }
    
    int maxPrio;
    bool foundPrio = false;
    // Get the largest priority
    auto prioIterator = priorities.begin();
    while (!foundPrio)
    {
        assert(prioIterator != priorities.end());
        maxPrio = prioIterator->first;
        // We can't use that priority if no state with the priority is included anymore
        foundPrio = std::any_of(priorities[maxPrio].begin(), priorities[maxPrio].end(), 
                                [&includedStates](int v) { return includedStates[v]; });
        if (!foundPrio) prioIterator++;
    }

    int i = maxPrio % 2;
    std::unordered_set<int> R = getAttractors(priorities[maxPrio], i == 0, includedStates);
    // Remove all attractors from the included states
    std::vector<bool> newIncludedStates{ includedStates };
    for (int a : R)
    {
        newIncludedStates[a] = false;
    }
    auto zielonkaRes = zielonka(newIncludedStates);
    if (zielonkaRes[1-i].empty()) 
    {
        R.insert(zielonkaRes[i].begin(), zielonkaRes[i].end());
        // Only set the first part of the pair
        res[i] = R;
        assert (res[1-i].empty());
    }
    else 
    {
        std::unordered_set<int> S = getAttractors({zielonkaRes[1-i].begin(), zielonkaRes[1-i].end()}, i != 0, includedStates);
        newIncludedStates = includedStates;
        for (int a : S)
        {
            newIncludedStates[a] = false;
        }
        auto zielonkaResI = zielonka(newIncludedStates);
        S.insert(zielonkaResI[1-i].begin(), zielonkaResI[1-i].end());
        res[i] = zielonkaResI[i];
        res[1 - i] = S;
    }
    return res;
}

int main(int argc, char** argv)
{
    oxidd::bdd_function var1 = manager.new_var();
    oxidd::bdd_function var2 = manager.new_var();

    oxidd::bdd_function boolF = var1 & ~var2;
    oxidd::util::slice<std::pair<oxidd::bdd_function, bool>> args{ {var1, true}, {var2, false} };
    std::cout << "Original: " << boolF.eval(args) << std::endl;
    std::vector<std::pair<oxidd::bdd_function, oxidd::bdd_function>> sub_vars { {var2, manager.t()} };
    oxidd::bdd_substitution sub { sub_vars.begin(), sub_vars.end() };
    auto newF = boolF.substitute(sub);
    oxidd::util::slice<std::pair<oxidd::bdd_function, bool>> args2{ {var1, true}, {var2, false} };
    std::cout << "Replaced: " << newF.eval(args2) << std::endl;

    HoaData hoa;
    defaultsHoa(&hoa);
    auto inputFile = fopen(argv[1], "r");
    int res = parseHoa(inputFile, &hoa);
    fclose(inputFile);
    if (res == 0) {
        std::cout << "Finished parsing the file\n";
    }
    else {
        std::cout << "Input could not be parsed!\n";
    }

    generateDotFile(&hoa, "inputHOA.dot");

    std::cout << "Creating AP variables...\n";
    for (size_t ap = 0; ap < hoa.noAPs; ap++)
    {
        aps[ap] = manager.new_var();
    }

    std::cout << "Creating aliases...\n";
    for (size_t a = 0; a < hoa.noAliases; a++)
    {
        aliases[hoa.aliases[a].alias] = hoa.aliases[a].labelExpr;
    }

    std::cout << "Creating controllable APs...\n";
    std::vector<bool> controllableAPs (hoa.noAPs, false);
    for (size_t c = 0; c < hoa.noCntAPs; c++)
    {
        controllableAPs[hoa.cntAPs[c]] = true;
    }
    
    std::cout << "Creating states, resizing to " << hoa.noStates << std::endl;
    PGstates.resize(hoa.noStates);
    for (size_t s = 0; s < hoa.noStates; s++)
    {
        State state = hoa.states[s];
        PGstates[s] = PGState{state.id};
        if (state.noAccSig < 1) {
            std::cout << "The state has no accSig...\n";
        }
        priorities[state.accSig[0]].push_back(s);
        
        std::unordered_set<int> allUncontrollableAP;
        std::vector<PGTransition> allOutgoing;
        // Collect all outgoing transitions and not controllable APs
        for (size_t t = 0; t < state.noTrans; t++)
        {
            assert(state.transitions[t].noSucc == 1);
            auto transition = btreeToBDD(state.transitions[t].label, controllableAPs);
            transition.target = state.transitions[t].successors[0];
            allOutgoing.push_back(transition);

            allUncontrollableAP.insert(transition.notControllable.begin(), transition.notControllable.end());
        }

        if (allUncontrollableAP.empty()) 
        {
            // The transitions are all entirely made up of controllable APs, we can just use it as is
            PGstates[s].belongsToEven = true;
            for (PGTransition t : allOutgoing) {
                assert (t.target != -1);
                outgoingTransitions[state.id][t.target] = t.label;
                incomingTransitions[t.target][state.id] = t.label;
            }
        }
        else 
        {
            int numNotControllable = allUncontrollableAP.size();
            // Possible combinations for the uncontrollable vars
            int numCombinations = 1 << numNotControllable; 
            
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
                PGstates.push_back({state.id, fixedTransition, true});
                // New intermediary state inherits priority from original
                priorities[state.accSig[0]].push_back(intermediaryState);
                outgoingTransitions[state.id][intermediaryState] = fixedTransition;
                incomingTransitions[intermediaryState][state.id] = fixedTransition;
                
                for (PGTransition t : allOutgoing) {
                    oxidd::bdd_function newLabel = fixedTransition & t.label;
                    if (newLabel.satisfiable()) {
                        assert (t.target != -1);
                        outgoingTransitions[intermediaryState][t.target] = newLabel;
                        incomingTransitions[t.target][intermediaryState] = newLabel;
                    }   
                }
            }
        }
        
    }

    std::cout << "Starting Zielonka...\n";
    std::vector<bool> included (PGstates.size(), true);
    auto [w_even, w_odd] = zielonka(included);
    std::cout << "Even player winning positions: " << w_even.size() << std::endl;
    std::cout << "Odd player winning positions: " << w_odd.size() << std::endl;
    assert(w_even.size() + w_odd.size() == PGstates.size());
    
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

    if(res != 0) {
        std::cout << argv[1] << "\n";
    }
    else {
        std::cout << "Fibnish!\n";
    }
}
