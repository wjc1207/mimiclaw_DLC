## Feat. 1. Two-Layer hardware management
- Swarmclaw manages hardware tools in two layers:
  - build-time layer: components are included/excluded at build time based on Kconfig options
  - runtime layer: tools can be enabled/disabled at runtime via CLI or web portal, allowing dynamic resource management and feature toggling without reflashing


## Feat. 2. A2A Communication Protocol
- Swarmclaw implements a custom Agent-to-Agent (A2A) communication protocol for seamless interaction between multiple agents and tools, enabling complex workflows and multi-agent collaboration on resource-constrained devices

## Feat. 3. lua scripting support
- Swarmclaw integrates Lua scripting support, allowing ai agents to write and execute Lua scripts for dynamic behavior, task automation, and on-the-fly adjustments to their operations, enhancing flexibility and adaptability in various scenarios
