"""Reference from-scratch encoder-decoder Transformer with source-pointer heads."""
from __future__ import annotations

from typing import Any

try:
    import torch
    from torch import nn
except ImportError:  # Keep data validation usable in lightweight environments.
    torch = None  # type: ignore[assignment]
    nn = None  # type: ignore[assignment]


class _MissingTorch:
    def __init__(self, *_: Any, **__: Any) -> None:
        raise RuntimeError("PyTorch is required for model construction; install training/requirements.txt on the training machine")


if nn is None:
    TextGraphTransformer = _MissingTorch  # type: ignore[misc,assignment]
else:
    class TextGraphTransformer(nn.Module):
        """Single shared-embedding Transformer; no pretrained checkpoint is loaded."""

        def __init__(self, vocab_size: int, config: dict[str, Any], max_target_positions: int = 4096) -> None:
            super().__init__()
            m = config["model"]
            d_model = int(m["d_model"])
            max_input = int(m["max_input_tokens"])
            heads = int(m["attention_heads"])
            dropout = float(m["dropout"])
            self.max_input_tokens = max_input
            self.max_target_positions = max_target_positions
            self.token_embedding = nn.Embedding(vocab_size, d_model)
            self.input_positions = nn.Embedding(max_input, d_model)
            self.target_positions = nn.Embedding(max_target_positions, d_model)
            self.dropout = nn.Dropout(dropout)
            enc_layer = nn.TransformerEncoderLayer(
                d_model=d_model, nhead=heads,
                dim_feedforward=int(m["feed_forward_dim"]), dropout=dropout,
                activation="gelu", batch_first=True, norm_first=True,
            )
            dec_layer = nn.TransformerDecoderLayer(
                d_model=d_model, nhead=heads,
                dim_feedforward=int(m["feed_forward_dim"]), dropout=dropout,
                activation="gelu", batch_first=True, norm_first=True,
            )
            self.encoder = nn.TransformerEncoder(enc_layer, num_layers=int(m["encoder_layers"]))
            self.decoder = nn.TransformerDecoder(dec_layer, num_layers=int(m["decoder_layers"]))
            self.final_norm = nn.LayerNorm(d_model)
            self.action_head = nn.Linear(d_model, vocab_size, bias=False)
            self.action_head.weight = self.token_embedding.weight
            self.pointer_start_query = nn.Linear(d_model, d_model, bias=False)
            self.pointer_end_query = nn.Linear(d_model * 2, d_model, bias=False)
            self.pointer_key = nn.Linear(d_model, d_model, bias=False)
            self._initialize(float(m.get("init_std", 0.02)))

        def _initialize(self, std: float) -> None:
            for module in self.modules():
                if isinstance(module, nn.Embedding):
                    nn.init.normal_(module.weight, mean=0.0, std=std)
                elif isinstance(module, nn.Linear):
                    nn.init.normal_(module.weight, mean=0.0, std=std)
                    if module.bias is not None:
                        nn.init.zeros_(module.bias)
                elif isinstance(module, nn.LayerNorm):
                    nn.init.ones_(module.weight)
                    nn.init.zeros_(module.bias)

        def forward(self, source_ids: Any, decoder_ids: Any,
                    source_padding_mask: Any | None = None,
                    decoder_padding_mask: Any | None = None,
                    pointer_start_for_end: Any | None = None) -> dict[str, Any]:
            batch, source_length = source_ids.shape
            _, target_length = decoder_ids.shape
            if source_length > self.max_input_tokens:
                raise ValueError("input exceeds configured maximum; truncation is forbidden")
            if target_length > self.max_target_positions:
                raise ValueError("decoder actions exceed configured limit")
            source_pos = torch.arange(source_length, device=source_ids.device).unsqueeze(0)
            target_pos = torch.arange(target_length, device=decoder_ids.device).unsqueeze(0)
            source = self.dropout(self.token_embedding(source_ids) + self.input_positions(source_pos))
            target = self.dropout(self.token_embedding(decoder_ids) + self.target_positions(target_pos))
            memory = self.encoder(source, src_key_padding_mask=source_padding_mask)
            causal = torch.triu(torch.ones(target_length, target_length, device=decoder_ids.device, dtype=torch.bool), diagonal=1)
            hidden = self.decoder(
                target, memory, tgt_mask=causal,
                tgt_key_padding_mask=decoder_padding_mask,
                memory_key_padding_mask=source_padding_mask,
            )
            hidden = self.final_norm(hidden)
            action_logits = self.action_head(hidden)
            keys = self.pointer_key(memory)
            start_query = self.pointer_start_query(hidden)
            start_logits = torch.einsum("bld,btd->blt", start_query, keys) / (keys.size(-1) ** 0.5)
            if source_padding_mask is not None:
                start_logits = start_logits.masked_fill(source_padding_mask[:, None, :], float("-inf"))
            if pointer_start_for_end is None:
                starts = start_logits.argmax(dim=-1)
            else:
                starts = pointer_start_for_end.clamp(min=0, max=source_length - 1)
            selected = memory.gather(1, starts.unsqueeze(-1).expand(batch, target_length, memory.size(-1)))
            end_query = self.pointer_end_query(torch.cat((hidden, selected), dim=-1))
            end_logits = torch.einsum("bld,btd->blt", end_query, keys) / (keys.size(-1) ** 0.5)
            if source_padding_mask is not None:
                end_logits = end_logits.masked_fill(source_padding_mask[:, None, :], float("-inf"))
            # Enforce end >= start as a structural pointer constraint, not a semantic choice.
            positions = torch.arange(source_length, device=source_ids.device).view(1, 1, -1)
            end_logits = end_logits.masked_fill(positions < starts.unsqueeze(-1), float("-inf"))
            return {"action_logits": action_logits, "pointer_start_logits": start_logits,
                    "pointer_end_logits": end_logits, "encoder_states": memory}
