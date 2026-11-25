//! Parent-child inheritance of the background process policy (F5.1).
//! An unknown child (a WebView2 helper, a crash reporter, a worker) takes the `Throttle`/`EcoOnly`
//! action of its nearest ancestor that has one. The link is cut, always, at the active game, at its
//! descendants, at Windows processes, at anything the policy protects (`DO_NOT_TOUCH` with a known class,
//! a user exception, an anti-cheat signal) and at any link that cannot be proven: parent gone,
//! PID reused (parent created after the child), unknown creation time, ambiguous PID, cycle.
//! Pure data in, one decision per node out; the exported C function is in `ffi/process.rs`.

use super::process_class::{
    VfProcessDecision, VF_PROC_ACTION_DO_NOT_TOUCH, VF_PROC_ACTION_ECO_ONLY, VF_PROC_ACTION_THROTTLE,
    VF_PROC_CLASS_UNKNOWN,
};
use std::collections::{HashMap, HashSet};

/// Deepest chain followed; real trees are far shallower, so hitting it means something is wrong: no inheritance.
pub const MAX_TREE_DEPTH: usize = 64;

/// One process of a snapshot with the decision the per-process rules gave it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ProcessNode {
    pub pid: u32,
    pub parent_pid: u32,
    /// FILETIME of the process creation; 0 when unreadable (then the link to its parent cannot be proven).
    pub creation_time: u64,
    pub decision: VfProcessDecision,
    /// The user listed this exe in the per-program exceptions: their choice is never overridden.
    pub has_user_exception: bool,
    pub has_anticheat_signal: bool,
}

fn is_background_action(action: u8) -> bool {
    action == VF_PROC_ACTION_THROTTLE || action == VF_PROC_ACTION_ECO_ONLY
}

/// Can only take the policy of an ancestor: unknown to the rules, untouched by default, nothing protecting it.
fn can_inherit(n: &ProcessNode, game_pid: u32) -> bool {
    n.pid != 0
        && n.pid != 4
        && n.pid != game_pid
        && n.decision.process_class == VF_PROC_CLASS_UNKNOWN
        && n.decision.action == VF_PROC_ACTION_DO_NOT_TOUCH
        && !n.has_user_exception
        && !n.has_anticheat_signal
}

enum Chain {
    /// Ancestors whose links are all proven, nearest first (indices into the nodes). The chain may stop early
    /// at a link that cannot be proven (parent gone, reused PID, unknown time, ambiguous PID, too deep):
    /// what was proven below that point still stands, nothing beyond it is guessed.
    Ancestors(Vec<usize>),
    /// The chain reaches the active game, or loops: nothing in it is ever touched.
    Excluded,
}

fn walk_up(nodes: &[ProcessNode], by_pid: &HashMap<u32, Option<usize>>, start: usize, game_pid: u32) -> Chain {
    let mut out = Vec::new();
    let mut seen: HashSet<u32> = HashSet::new();
    seen.insert(nodes[start].pid);
    let mut cur = start;
    loop {
        let child = &nodes[cur];
        let ppid = child.parent_pid;
        if ppid == 0 {
            return Chain::Ancestors(out);
        }
        if !seen.insert(ppid) {
            return Chain::Excluded; // cycle, including a process that is its own parent
        }
        match by_pid.get(&ppid) {
            Some(Some(pi)) => {
                let parent = &nodes[*pi];
                if child.creation_time == 0 || parent.creation_time == 0 || parent.creation_time > child.creation_time {
                    return Chain::Ancestors(out); // reused PID, or a time we cannot read
                }
                if ppid == game_pid {
                    return Chain::Excluded;
                }
                if out.len() >= MAX_TREE_DEPTH {
                    return Chain::Ancestors(out);
                }
                out.push(*pi);
                cur = *pi;
            }
            Some(None) => return Chain::Ancestors(out), // two live entries with that PID
            None => {
                // Parent gone. If it was the game, its leftovers are still the game's children.
                return if ppid == game_pid { Chain::Excluded } else { Chain::Ancestors(out) };
            }
        }
    }
}

/// The effective decision of every node, in the same order. Only nodes that `can_inherit` can change.
pub fn inherit_decisions(nodes: &[ProcessNode], active_game_pid: u32) -> Vec<VfProcessDecision> {
    let mut by_pid: HashMap<u32, Option<usize>> = HashMap::with_capacity(nodes.len());
    for (i, n) in nodes.iter().enumerate() {
        by_pid.entry(n.pid).and_modify(|e| *e = None).or_insert(Some(i));
    }
    nodes
        .iter()
        .enumerate()
        .map(|(i, n)| {
            if !can_inherit(n, active_game_pid) {
                return n.decision;
            }
            let Chain::Ancestors(chain) = walk_up(nodes, &by_pid, i, active_game_pid) else {
                return n.decision;
            };
            for &ai in &chain {
                let a = &nodes[ai];
                if is_background_action(a.decision.action) {
                    return VfProcessDecision {
                        process_class: a.decision.process_class,
                        action: a.decision.action,
                        protected_by_anticheat: false,
                        path_verified: false,
                    };
                }
                if !can_inherit(a, active_game_pid) {
                    break; // known DO_NOT_TOUCH, Windows, game-like, excepted or anti-cheat: the cut
                }
            }
            n.decision
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::policy::process_class::{
        classify_process, ProcessPolicyInput, VfProcessCategoryActions, VF_PROC_ACTION_BOOST_AS_GAME,
        VF_PROC_CLASS_CALLS_VOICE, VF_PROC_CLASS_MESSAGING, VF_PROC_CLASS_WINDOWS_SYSTEM,
    };

    const GAME: u32 = 900;

    fn dec(class: u8, action: u8) -> VfProcessDecision {
        VfProcessDecision { process_class: class, action, protected_by_anticheat: false, path_verified: true }
    }

    fn node(pid: u32, parent_pid: u32, creation_time: u64, decision: VfProcessDecision) -> ProcessNode {
        ProcessNode { pid, parent_pid, creation_time, decision, has_user_exception: false, has_anticheat_signal: false }
    }

    fn unknown(pid: u32, parent_pid: u32, t: u64) -> ProcessNode {
        node(pid, parent_pid, t, dec(VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_DO_NOT_TOUCH))
    }

    fn throttled(pid: u32, parent_pid: u32, t: u64) -> ProcessNode {
        node(pid, parent_pid, t, dec(VF_PROC_CLASS_MESSAGING, VF_PROC_ACTION_THROTTLE))
    }

    fn actions(nodes: &[ProcessNode]) -> Vec<u8> {
        inherit_decisions(nodes, GAME).iter().map(|d| d.action).collect()
    }

    #[test]
    fn child_of_throttled_parent_is_throttled() {
        let n = [throttled(10, 1, 100), unknown(11, 10, 200)];
        let d = inherit_decisions(&n, GAME);
        assert_eq!(d[1].action, VF_PROC_ACTION_THROTTLE);
        assert_eq!(d[1].process_class, VF_PROC_CLASS_MESSAGING);
        assert_eq!(d[0], n[0].decision);
    }

    #[test]
    fn eco_only_is_inherited_as_eco_only() {
        let n = [node(10, 1, 100, dec(VF_PROC_CLASS_MESSAGING, VF_PROC_ACTION_ECO_ONLY)), unknown(11, 10, 200)];
        assert_eq!(actions(&n), vec![VF_PROC_ACTION_ECO_ONLY, VF_PROC_ACTION_ECO_ONLY]);
    }

    #[test]
    fn grandchild_inherits_through_unknown_middle() {
        let n = [throttled(10, 1, 100), unknown(11, 10, 200), unknown(12, 11, 300)];
        assert_eq!(actions(&n)[2], VF_PROC_ACTION_THROTTLE);
    }

    #[test]
    fn parent_that_already_exited_gives_nothing() {
        let n = [unknown(11, 77, 200)]; // 77 is not in the snapshot
        assert_eq!(actions(&n), vec![VF_PROC_ACTION_DO_NOT_TOUCH]);
    }

    #[test]
    fn exited_middle_link_breaks_the_chain() {
        // throttled(10) -> [gone 11] -> 12: 12 must not skip the gap to 10.
        let n = [throttled(10, 1, 100), unknown(12, 11, 300)];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn reused_pid_is_not_a_parent() {
        // The child (t=100) is older than the process now holding its parent's PID (t=500): not its parent.
        let n = [throttled(10, 1, 500), unknown(11, 10, 100)];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn same_creation_time_still_counts_as_parent() {
        let n = [throttled(10, 1, 100), unknown(11, 10, 100)];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_THROTTLE);
    }

    #[test]
    fn unreadable_creation_time_gives_nothing() {
        let n = [throttled(10, 1, 0), unknown(11, 10, 200)];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_DO_NOT_TOUCH);
        let n = [throttled(10, 1, 100), unknown(11, 10, 0)];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn two_live_entries_with_one_pid_are_ambiguous() {
        let n = [throttled(10, 1, 100), unknown(10, 1, 100), unknown(11, 10, 200)];
        assert_eq!(actions(&n)[2], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn cycle_terminates_and_gives_nothing() {
        // 20 <-> 21, plus 22 hanging from the cycle, plus a process that is its own parent.
        let n = [unknown(20, 21, 100), unknown(21, 20, 100), unknown(22, 21, 200), unknown(23, 23, 100)];
        assert_eq!(actions(&n), vec![VF_PROC_ACTION_DO_NOT_TOUCH; 4]);
    }

    #[test]
    fn throttled_parent_inside_a_cycle_does_not_lend_its_policy() {
        // 30 (throttled) <-> 31 (unknown): 31 would inherit from 30 by one step, but the chain is a cycle.
        let n = [throttled(30, 31, 100), unknown(31, 30, 100)];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn chain_deeper_than_the_cap_gives_nothing() {
        let mut n = vec![throttled(1000, 1, 1)];
        for i in 1..=(MAX_TREE_DEPTH as u32 + 2) {
            n.push(unknown(1000 + i, 1000 + i - 1, 1 + i as u64));
        }
        let d = actions(&n);
        assert_eq!(d[1], VF_PROC_ACTION_THROTTLE);
        assert_eq!(*d.last().unwrap(), VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn active_game_and_its_children_are_never_touched() {
        // steam (throttled) launches the game; the game's helper and the helper's child stay free.
        let n = [
            throttled(10, 1, 100),
            node(GAME, 10, 200, dec(VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_BOOST_AS_GAME)),
            unknown(901, GAME, 300),
            unknown(902, 901, 400),
        ];
        let d = actions(&n);
        assert_eq!(d[1], VF_PROC_ACTION_BOOST_AS_GAME);
        assert_eq!(d[2], VF_PROC_ACTION_DO_NOT_TOUCH);
        assert_eq!(d[3], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn game_that_left_orphans_still_protects_them() {
        // The game node is not in the snapshot (it exited); its children name it as parent.
        let n = [unknown(901, GAME, 300)];
        assert_eq!(actions(&n), vec![VF_PROC_ACTION_DO_NOT_TOUCH]);
    }

    #[test]
    fn grandchild_of_game_under_a_throttled_process_stays_free() {
        // game -> browser (throttled by its own rules) -> helper: the helper descends from the game.
        let n = [
            node(GAME, 1, 100, dec(VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_BOOST_AS_GAME)),
            throttled(50, GAME, 200),
            unknown(51, 50, 300),
        ];
        assert_eq!(actions(&n)[2], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn game_pid_reused_by_a_newer_process_is_not_its_parent_link() {
        // `old` claims GAME as parent but is older than the process now holding that PID.
        let n = [
            node(GAME, 1, 500, dec(VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_BOOST_AS_GAME)),
            unknown(60, GAME, 100),
        ];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_DO_NOT_TOUCH); // broken link: untouched either way
    }

    #[test]
    fn windows_parent_cuts_the_chain() {
        let n = [
            node(10, 1, 100, dec(VF_PROC_CLASS_WINDOWS_SYSTEM, VF_PROC_ACTION_DO_NOT_TOUCH)),
            unknown(11, 10, 200),
        ];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn windows_process_in_the_middle_cuts_the_chain() {
        // slack (throttled) -> explorer-like Windows process -> unknown
        let n = [
            throttled(10, 1, 100),
            node(11, 10, 200, dec(VF_PROC_CLASS_WINDOWS_SYSTEM, VF_PROC_ACTION_DO_NOT_TOUCH)),
            unknown(12, 11, 300),
        ];
        assert_eq!(actions(&n)[2], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn known_do_not_touch_parent_cuts_the_chain() {
        // calls_voice defaults to DO_NOT_TOUCH: its helpers stay as free as it is.
        let n = [node(10, 1, 100, dec(VF_PROC_CLASS_CALLS_VOICE, VF_PROC_ACTION_DO_NOT_TOUCH)), unknown(11, 10, 200)];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn windows_process_is_never_changed_even_under_a_throttled_parent() {
        let n = [
            throttled(10, 1, 100),
            node(11, 10, 200, dec(VF_PROC_CLASS_WINDOWS_SYSTEM, VF_PROC_ACTION_DO_NOT_TOUCH)),
        ];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn boosted_possible_game_in_the_middle_cuts_and_is_not_changed() {
        let n = [
            throttled(10, 1, 100),
            node(11, 10, 200, dec(VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_BOOST_AS_GAME)),
            unknown(12, 11, 300),
        ];
        let d = actions(&n);
        assert_eq!(d[1], VF_PROC_ACTION_BOOST_AS_GAME);
        assert_eq!(d[2], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn user_exception_child_keeps_the_user_choice() {
        let mut child = unknown(11, 10, 200);
        child.has_user_exception = true; // the user pinned it to "do not touch"
        let n = [throttled(10, 1, 100), child];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn user_exception_parent_pinned_do_not_touch_cuts_the_chain() {
        let mut mid = unknown(11, 10, 200);
        mid.has_user_exception = true;
        let n = [throttled(10, 1, 100), mid, unknown(12, 11, 300)];
        assert_eq!(actions(&n)[2], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn anticheat_child_is_never_inherited_onto_and_cuts_below() {
        let mut ac = unknown(11, 10, 200);
        ac.has_anticheat_signal = true;
        let n = [throttled(10, 1, 100), ac, unknown(12, 11, 300)];
        let d = actions(&n);
        assert_eq!(d[1], VF_PROC_ACTION_DO_NOT_TOUCH);
        assert_eq!(d[2], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn own_decision_of_a_child_wins_over_inheritance() {
        let n = [throttled(10, 1, 100), node(11, 10, 200, dec(VF_PROC_CLASS_MESSAGING, VF_PROC_ACTION_ECO_ONLY))];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_ECO_ONLY);
    }

    #[test]
    fn system_pids_are_never_inherited_onto() {
        let n = [throttled(10, 0, 100), unknown(4, 10, 200), unknown(0, 10, 200)];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn empty_snapshot_is_empty() {
        assert!(inherit_decisions(&[], GAME).is_empty());
    }

    // --- Vectors from the real rules: the decision of each node comes from classify_process. ---

    fn decide(path: &str, cats: VfProcessCategoryActions) -> VfProcessDecision {
        classify_process(&ProcessPolicyInput {
            full_path: path,
            exe_name: "",
            category_actions: cats,
            exceptions: &[],
            custom_game_exes: &[],
            excluded_game_exes: &[],
            heavy_work_as_game: false,
            is_foreground_3d: false,
            has_anticheat_signal: false,
        })
    }

    #[test]
    fn webview2_of_teams_is_throttled_when_teams_is() {
        let cats = VfProcessCategoryActions { calls_voice: VF_PROC_ACTION_THROTTLE, ..Default::default() };
        let teams = decide(r"C:\Program Files\WindowsApps\MSTeams_1\ms-teams.exe", cats);
        let web = decide(r"C:\Program Files (x86)\Microsoft\EdgeWebView\Application\1\msedgewebview2.exe", cats);
        assert_eq!(teams.action, VF_PROC_ACTION_THROTTLE);
        assert_eq!(web.process_class, VF_PROC_CLASS_UNKNOWN);
        let n = [node(10, 1, 100, teams), node(11, 10, 200, web)];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_THROTTLE);
    }

    #[test]
    fn webview2_of_teams_is_free_with_default_calls_policy() {
        let cats = VfProcessCategoryActions::default(); // calls_voice = do not touch
        let teams = decide(r"C:\Program Files\WindowsApps\MSTeams_1\ms-teams.exe", cats);
        let web = decide(r"C:\Program Files (x86)\Microsoft\EdgeWebView\Application\1\msedgewebview2.exe", cats);
        let n = [node(10, 1, 100, teams), node(11, 10, 200, web)];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn steam_launches_the_game_and_the_game_is_not_throttled() {
        let cats = VfProcessCategoryActions::default(); // stores_launchers = throttle
        let steam = decide(r"C:\Program Files (x86)\Steam\steam.exe", cats);
        assert_eq!(steam.action, VF_PROC_ACTION_THROTTLE);
        let game = decide(r"D:\Steam\steamapps\common\Octopath\Octopath.exe", cats);
        let n = [
            node(10, 1, 100, steam),
            node(GAME, 10, 200, dec(VF_PROC_CLASS_UNKNOWN, VF_PROC_ACTION_BOOST_AS_GAME)),
            node(901, GAME, 300, decide(r"D:\Steam\steamapps\common\Octopath\crashhandler.exe", cats)),
        ];
        assert_ne!(game.action, VF_PROC_ACTION_THROTTLE);
        let d = actions(&n);
        assert_eq!(d[1], VF_PROC_ACTION_BOOST_AS_GAME);
        assert_eq!(d[2], VF_PROC_ACTION_DO_NOT_TOUCH);
    }

    #[test]
    fn steam_helper_that_is_not_the_active_game_is_throttled() {
        // An unknown tool Steam started (not the game in front) follows Steam's policy.
        let cats = VfProcessCategoryActions::default();
        let steam = decide(r"C:\Program Files (x86)\Steam\steam.exe", cats);
        let n = [node(10, 1, 100, steam), unknown(11, 10, 200)];
        assert_eq!(actions(&n)[1], VF_PROC_ACTION_THROTTLE);
    }
}
