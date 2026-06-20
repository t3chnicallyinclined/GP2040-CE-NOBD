# NOBD Position Statement

## The whole feature in one picture

Stock GP2040-CE already waits with a 5ms debounce by default to filter switch noise. NOBD sits in that exact same slot and uses the same default 5ms, spending it to wait for your two presses to land together instead:

```
Stock GP2040-CE (default):  press -> GPIO -> debounce: 5ms noise filter       -> state -> USB report
NOBD (default):             press -> GPIO -> sync window: 5ms, wait for intent -> state -> USB report
```

Same pipeline. Same slot. Same default 5ms. The only change is what the 5ms does. (NOBD's wait happens before the press commits, so it costs up to 5ms of first-press latency, which is the honest tradeoff.)

## What NOBD is

A controller has one job: deliver what you meant to do to the game intact.

**Intent** is what you meant. "Both punches, together."

**Resolution** is the window a system uses to read that intent. Nothing reads your mind, so every layer infers what you meant from a slice of time:

- The game reads at 16ms (one frame). Anything inside a frame, it treats as one intended moment.
- Human intent lands at 2 to 8ms. Press two buttons "together" and your fingers are that far apart. That gap is one intention, not two. (Measure your own with the [Finger Gap Tester](https://github.com/t3chnicallyinclined/finger-gap-tester).)
- USB reads at 1ms.

The whole problem in one line: **USB reads finer than human intent resolves.** At 1ms, a single intention spread across a 3ms finger gap is sliced into two inputs and handed to the game as if you meant them apart. That is not a quirk of the platform. It is a measurement error, reading intent at a resolution where intent does not resolve.

Every input layer is already an intent-reading system. The 16ms frame, input buffers, debounce, and SOCD cleaning all exist to infer what you meant from imperfect signals. Intent is not a word invented to defend NOBD. It is the operating principle the whole stack already runs on.

NOBD restores a window wide enough to hold one human intent, and aims it at your press so it never slices one in half.

USB polling already samples input on a fixed 1ms clock, and stock firmware sends each change as soon as the USB endpoint is free. It does not wait or group, so a 2 to 8ms finger gap reliably splits across polls. NOBD replaces that with a deliberate window, sized to a finger gap (5ms) and anchored to your press, so the presses group instead of split.

## What NOBD does and does not do

It does:

- Change WHEN a real press is reported, never WHICH buttons.
- Group presses you physically made so they land on the same frame.
- Cost you latency to do it. A tradeoff, not a free gain.

It does not:

- Invent, predict, or fabricate any input. You press both buttons, or nothing happens.
- Automate anything. It cannot dash, tech, or act for you.
- Delay your releases (default). Negative edge and fast inputs stay as quick as a raw stick.

The entire feature is one function, public and auditable: [src/gp2040.cpp](https://github.com/t3chnicallyinclined/GP2040-CE-NOBD/blob/main/src/gp2040.cpp).

## On fairness

NOBD is in the same family as features every legal controller already runs. Debounce filters switch noise. Deadzones condition analog input. SOCD cleaning resolves impossible directions, and is required at tournaments. All of them condition your raw input to preserve intent. None automate anything. NOBD is the same category.

Cheating fabricates intent. An aimbot aims for you, a macro presses for you, OBD fires two buttons from one. Every cheat acts on intent you never performed. NOBD acts only on intent you physically demonstrated. One invents what you meant. The other delivers it.

NOBD breaks no existing tournament rule. It is not a macro, turbo, rapid-fire, or compound input. Whether a tournament should write a rule about it is a decision for tournament organizers and the community, not for us, and not for any other firmware project.

## On being one implementation among many

Every fightstick firmware is an opinionated implementation. GP2040-CE makes choices: how it debounces, which SOCD modes it ships, how it reports state. Brook makes choices too, behind closed source nobody can read. Every board on the market is some maker's private opinion about how a controller should turn your fingers into inputs. There is no neutral, "true" firmware. There are only implementations, each with a point of view.

NOBD is ours, and it is the only one you can actually read. The closed boards ask you to trust their choices blind. We published every line of ours. If you think our choice is wrong, point at the exact code and say why. Nobody can do that with a Brook.

And no firmware project gets to rule a tournament. "Cheating" is a determination made by tournament organizers and the community against an actual ruleset. A maintainer of a competing implementation calling our choice cheating is stating an opinion, and their opinion carries exactly as much official weight as ours, which is none. We are not the arbiter either. Neither are they.

You can disagree with the design. That is a design opinion. It is not a cheating verdict, and no one outside a ruleset gets to make it one.

## Verify it yourself

Don't take our word for any of this:

- **Read the code.** The whole feature is one function: [src/gp2040.cpp](https://github.com/t3chnicallyinclined/GP2040-CE-NOBD/blob/main/src/gp2040.cpp).
- **Measure your own finger gap** with the [Finger Gap Tester](https://github.com/t3chnicallyinclined/finger-gap-tester).
- **See the evidence** that the problem is real: [Why NOBD Exists](WHY-NOBD.md).

Open source exists precisely so people can build their own implementations. That is what we did, in the open. Read it, and decide for yourself.
