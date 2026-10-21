# Annotated UI wireframes

These original 1600×900 vector diagrams illustrate hierarchy and states. They are not screenshots or proof of an implemented game. Sample own values and item contents are illustrative, not normal-rule progression evidence. Exact native sizing/reflow is specified in the presentation document.

| Diagram | SVG source | PNG preview |
| --- | --- | --- |
| World HUD and selected-unit inspector | [01_HUD_SELECTION.svg](01_HUD_SELECTION.svg) | [01_HUD_SELECTION.png](01_HUD_SELECTION.png) |
| Large bag and quick-item references | [02_INVENTORY_QUICK_ITEMS.svg](02_INVENTORY_QUICK_ITEMS.svg) | [02_INVENTORY_QUICK_ITEMS.png](02_INVENTORY_QUICK_ITEMS.png) |
| Peer final review | [03_PEER_TRADE_REVIEW.svg](03_PEER_TRADE_REVIEW.svg) | [03_PEER_TRADE_REVIEW.png](03_PEER_TRADE_REVIEW.png) |

HUD: the selected stranger's private data stays Unknown while Yours remains anchored. Inventory: each icon occupies carried space; quick references do not. Trade: the other party's full wallet/bag is absent, and both final confirmations follow Ready. Use native state-driven widgets in development rather than importing these images as a fake working interface.

The inventory grid reflows to four columns in the compact layout. The final Confirm/Cancel strip and both trade directions stay visible. Add native screenshots for ready, pending, blocked, full, death, hidden target and delayed network states during actual development.
