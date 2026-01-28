Trinity
===================================

What is Trinity?
------------------

Trinity is a cryptocurrency that implements a multi-algorithm Proof of Work system with three mining algorithms and dynamic reward allocation.

Key Features
------------------

- **Multi-Algorithm Mining**: Support for three different mining algorithms
  - SHA256d (default)
  - Scrypt
  - Groestl
- **Random Rewards**: Dynamic reward system for mining
- **Decentralized Network**: Peer-to-peer cryptocurrency network

Building Trinity
------------------

### Dependencies

Trinity requires the following dependencies to build:

```bash
sudo apt-get update
sudo apt-get install build-essential libboost1.49-dev libssl-dev libdb4.8-dev libdb4.8++-dev libqrencode-dev libminiupnpc-dev git
```

### Compilation

To build the Trinity daemon:

```bash
make -f Makefile
```

For the Qt GUI wallet:

```bash
qmake trinity-qt.pro
make
```

Network Infrastructure
------------------

Trinity uses a combination of DNS seeders and hardcoded seed nodes for peer discovery:

- **DNS Seeders**: The network supports DNS-based peer discovery for automatic node bootstrapping
- **Seed Nodes**: Fallback hardcoded seed nodes ensure network connectivity even when DNS seeders are unavailable
- **Seeder Tools**: Utilities for generating and maintaining seed node lists are available in `contrib/seeds/`

License
-------

Trinity is released under the terms of the MIT license. See `COPYING` for more
information or see http://opensource.org/licenses/MIT.
