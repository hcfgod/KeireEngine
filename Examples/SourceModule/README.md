# Source-module consumer

This managed SDK example builds a source-level module into one static pack, links that pack into the host, and passes
the module instances through `ApplicationSpecification::Modules`. Runtime dynamic loading and binary plugin ABI
compatibility are intentionally not part of this contract.

Configure with `CMAKE_PREFIX_PATH` pointing at an extracted Kéire SDK, build, then run
`SourceModuleConsumer --module-smoke`.
