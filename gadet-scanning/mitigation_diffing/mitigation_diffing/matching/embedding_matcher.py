#!/usr/bin/env python3
"""
Semantic code matching using UniXcoder embeddings.

Uses Microsoft's UniXcoder model to encode code snippets into dense vectors,
enabling semantic similarity comparison even when implementations differ syntactically.

This is particularly useful for cross-architecture comparison where:
- Variable names differ
- Control flow structure varies slightly
- Architecture-specific constructs are used
"""

import logging
import pickle
from pathlib import Path
from typing import Dict, List, Optional, Tuple
import numpy as np

# Lazy imports for heavy dependencies
_model = None
_tokenizer = None
_device = None


def _load_model():
    """Lazy load UniXcoder model (downloads on first use)."""
    global _model, _tokenizer, _device

    if _model is not None:
        return _model, _tokenizer, _device

    try:
        import torch
        from transformers import AutoModel, AutoTokenizer
    except ImportError:
        raise ImportError(
            "UniXcoder requires transformers and torch. Install with:\n"
            "  pip install transformers torch"
        )

    model_name = "microsoft/unixcoder-base-nine"  # Supports C language
    logging.info(f"Loading UniXcoder model: {model_name}")

    _tokenizer = AutoTokenizer.from_pretrained(model_name)
    _model = AutoModel.from_pretrained(model_name)

    # Use GPU if available
    _device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    _model = _model.to(_device)
    _model.eval()

    logging.info(f"UniXcoder loaded on device: {_device}")
    return _model, _tokenizer, _device


class EmbeddingMatcher:
    """Semantic code matching using UniXcoder embeddings."""

    def __init__(self, cache_dir: Optional[str] = None):
        """
        Initialize embedding matcher.

        Args:
            cache_dir: Optional directory to cache embeddings
        """
        self.cache_dir = Path(cache_dir) if cache_dir else Path('.embedding_cache')
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        self._embedding_cache: Dict[str, np.ndarray] = {}

        # Disk cache: arch -> {key: embedding}
        self._disk_cache: Dict[str, Dict[str, np.ndarray]] = {}
        self._disk_cache_funcs: Dict[str, List[Dict]] = {}

        # Lazy load model on first use
        self._model_loaded = False

    def _ensure_model_loaded(self):
        """Ensure model is loaded before use."""
        if not self._model_loaded:
            _load_model()
            self._model_loaded = True

    def extract_function_text(self, file_path: str, line_num: int,
                              context_lines: int = 500) -> Optional[str]:
        """
        Extract function body text from file.

        Uses simple heuristics to find function boundaries.
        More robust than line-counting for variable-length functions.

        Args:
            file_path: Path to source file
            line_num: Line number where function starts
            context_lines: Max lines to include

        Returns:
            Function body text or None if extraction fails
        """
        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                lines = f.readlines()
        except Exception as e:
            logging.debug(f"Failed to read {file_path}: {e}")
            return None

        if line_num < 1 or line_num > len(lines):
            return None

        # Find function start (may be a few lines before reported line)
        start_idx = max(0, line_num - 5)

        # Look for function definition pattern
        func_start = None
        for i in range(start_idx, min(line_num + 2, len(lines))):
            line = lines[i]
            # Simple heuristic: line with '(' and '{' or ')' followed by '{'
            if '(' in line and ('{' in line or
                (i + 1 < len(lines) and '{' in lines[i + 1])):
                func_start = i
                break

        if func_start is None:
            # Fallback: just use reported line
            func_start = line_num - 1

        # Find function end by brace matching
        brace_count = 0
        started = False
        func_end = min(func_start + context_lines, len(lines))

        for i in range(func_start, min(func_start + context_lines, len(lines))):
            line = lines[i]
            for char in line:
                if char == '{':
                    brace_count += 1
                    started = True
                elif char == '}':
                    brace_count -= 1

            if started and brace_count <= 0:
                func_end = i + 1
                break

        # Extract function text
        func_text = ''.join(lines[func_start:func_end])

        # Basic cleanup
        func_text = self._preprocess_code(func_text)

        return func_text

    def _preprocess_code(self, code: str) -> str:
        """
        Preprocess code for embedding.

        Removes architecture-specific noise while preserving semantics.
        """
        import re

        # Remove single-line comments
        code = re.sub(r'//.*$', '', code, flags=re.MULTILINE)

        # Remove multi-line comments
        code = re.sub(r'/\*.*?\*/', '', code, flags=re.DOTALL)

        # Normalize whitespace (but keep structure)
        code = re.sub(r'\t', '    ', code)
        code = re.sub(r' +', ' ', code)

        return code.strip()

    def encode(self, code: str, use_cache: bool = True) -> np.ndarray:
        """
        Encode code snippet to embedding vector.

        Args:
            code: Source code text
            use_cache: Whether to use embedding cache

        Returns:
            Embedding vector (768-dimensional for UniXcoder)
        """
        self._ensure_model_loaded()

        # Check cache (use code string directly as key)
        if use_cache and code in self._embedding_cache:
            return self._embedding_cache[code]

        import torch
        model, tokenizer, device = _load_model()

        # Tokenize with truncation
        inputs = tokenizer(
            code,
            return_tensors="pt",
            truncation=True,
            max_length=512,
            padding=True
        )
        inputs = {k: v.to(device) for k, v in inputs.items()}

        # Get embedding
        with torch.no_grad():
            outputs = model(**inputs)
            # Use [CLS] token embedding (first token)
            embedding = outputs.last_hidden_state[:, 0, :].cpu().numpy()

        embedding = embedding.squeeze()

        # Normalize for cosine similarity
        embedding = embedding / np.linalg.norm(embedding)

        # Cache result
        if use_cache:
            self._embedding_cache[code] = embedding

        return embedding

    def compute_similarity(self, emb1: np.ndarray, emb2: np.ndarray) -> float:
        """
        Compute cosine similarity between two embeddings.

        Args:
            emb1: First embedding vector
            emb2: Second embedding vector

        Returns:
            Similarity score in [0, 1] (normalized from [-1, 1])
        """
        # Embeddings are already normalized, so dot product = cosine similarity
        similarity = np.dot(emb1, emb2)

        # Normalize to [0, 1] range
        return (similarity + 1) / 2

    def compare_functions(self, file1: str, line1: int,
                          file2: str, line2: int) -> float:
        """
        Compare two functions using embedding similarity.

        Args:
            file1: Path to first source file
            line1: Line number in first file
            file2: Path to second source file
            line2: Line number in second file

        Returns:
            Similarity score in [0, 1]
        """
        # Extract function text
        text1 = self.extract_function_text(file1, line1)
        text2 = self.extract_function_text(file2, line2)

        if not text1 or not text2:
            logging.debug(f"Failed to extract function text")
            return 0.0

        # Skip very short functions (likely macros or stubs)
        if len(text1) < 20 or len(text2) < 20:
            return 0.0

        # Encode and compare
        emb1 = self.encode(text1)
        emb2 = self.encode(text2)

        return self.compute_similarity(emb1, emb2)

    def compare_code_strings(self, code1: str, code2: str) -> float:
        """
        Compare two code strings directly using embedding similarity.

        Args:
            code1: First code snippet
            code2: Second code snippet

        Returns:
            Similarity score in [0, 1]
        """
        if not code1 or not code2:
            return 0.0

        # Preprocess both
        code1 = self._preprocess_code(code1)
        code2 = self._preprocess_code(code2)

        # Encode and compare
        emb1 = self.encode(code1)
        emb2 = self.encode(code2)

        return self.compute_similarity(emb1, emb2)

    def batch_encode(self, code_snippets: List[str],
                     batch_size: int = 32) -> np.ndarray:
        """
        Encode multiple code snippets in batches for efficiency.

        Args:
            code_snippets: List of code strings
            batch_size: Batch size for encoding

        Returns:
            Array of embeddings (N x 768)
        """
        self._ensure_model_loaded()

        import torch
        model, tokenizer, device = _load_model()

        all_embeddings = []

        for i in range(0, len(code_snippets), batch_size):
            batch = code_snippets[i:i + batch_size]

            # Preprocess batch
            batch = [self._preprocess_code(code) for code in batch]

            # Tokenize batch
            inputs = tokenizer(
                batch,
                return_tensors="pt",
                truncation=True,
                max_length=512,
                padding=True
            )
            inputs = {k: v.to(device) for k, v in inputs.items()}

            # Get embeddings
            with torch.no_grad():
                outputs = model(**inputs)
                embeddings = outputs.last_hidden_state[:, 0, :].cpu().numpy()

            # Normalize
            embeddings = embeddings / np.linalg.norm(embeddings, axis=1, keepdims=True)

            all_embeddings.append(embeddings)

        return np.vstack(all_embeddings)

    def rank_candidates(self, source_code: str, candidates: List[str],
                        top_k: int = 10) -> List[Tuple[int, float]]:
        """
        Rank candidates by similarity to source code.

        Args:
            source_code: Source function text
            candidates: List of candidate function texts
            top_k: Number of top results to return

        Returns:
            List of (index, similarity) tuples, sorted by similarity descending
        """
        if not source_code or not candidates:
            return []

        # Encode source
        source_emb = self.encode(self._preprocess_code(source_code))

        # Batch encode candidates
        candidate_embs = self.batch_encode(candidates)

        # Compute similarities (dot product with normalized vectors)
        similarities = np.dot(candidate_embs, source_emb)

        # Normalize to [0, 1]
        similarities = (similarities + 1) / 2

        # Get top-k indices
        top_indices = np.argsort(similarities)[::-1][:top_k]

        return [(int(idx), float(similarities[idx])) for idx in top_indices]

    def clear_cache(self):
        """Clear embedding cache to free memory."""
        self._embedding_cache.clear()

    # Disk caching methods

    def _cache_path(self, arch: str) -> Path:
        """Get cache file path for architecture."""
        return self.cache_dir / f"{arch}_embeddings.pkl"

    def has_cached(self, arch: str) -> bool:
        """Check if embeddings are cached for architecture."""
        return self._cache_path(arch).exists()

    def load_cache(self, arch: str) -> bool:
        """Load cached embeddings from disk."""
        cache_file = self._cache_path(arch)
        if not cache_file.exists():
            return False

        with open(cache_file, 'rb') as f:
            data = pickle.load(f)
            self._disk_cache[arch] = data['embeddings']
            self._disk_cache_funcs[arch] = data['functions']

        return True

    def save_cache(self, arch: str, functions: List[Dict], embeddings: np.ndarray):
        """Save embeddings to disk cache."""
        cache_file = self._cache_path(arch)

        # Build key -> embedding mapping
        emb_dict = {}
        for func, emb in zip(functions, embeddings):
            key = f"{func['file']}:{func['line']}:{func['name']}"
            emb_dict[key] = emb

        with open(cache_file, 'wb') as f:
            pickle.dump({
                'embeddings': emb_dict,
                'functions': functions,
            }, f)

        # Also load into memory
        self._disk_cache[arch] = emb_dict
        self._disk_cache_funcs[arch] = functions

    def get_cached_functions(self, arch: str) -> List[Dict]:
        """Get list of cached functions for architecture."""
        if arch not in self._disk_cache_funcs:
            self.load_cache(arch)
        return self._disk_cache_funcs.get(arch, [])

    def get_cached_embeddings(self, arch: str) -> Optional[np.ndarray]:
        """Get cached embeddings matrix for architecture."""
        if arch not in self._disk_cache:
            if not self.load_cache(arch):
                return None
        emb_dict = self._disk_cache[arch]
        if not emb_dict:
            return None
        return np.vstack(list(emb_dict.values()))

    def cache_functions(self, arch: str, functions: List[Dict],
                        batch_size: int = 32, progress_callback=None) -> int:
        """
        Pre-compute and cache embeddings for functions.

        Args:
            arch: Architecture name (e.g., 'x86', 'riscv')
            functions: List of function dicts with 'file', 'line', 'name'
            batch_size: Batch size for encoding
            progress_callback: Optional callback(current, total) for progress

        Returns:
            Number of functions cached
        """
        self._ensure_model_loaded()

        # Extract function texts
        valid_funcs = []
        texts = []

        for i, func in enumerate(functions):
            text = self.extract_function_text(func['file'], func['line'])
            if text and len(text) >= 20:
                valid_funcs.append(func)
                texts.append(text)

            if progress_callback and i % 100 == 0:
                progress_callback(i, len(functions), "extracting")

        if not texts:
            return 0

        # Batch encode
        embeddings = self.batch_encode(texts, batch_size=batch_size)

        # Save to disk
        self.save_cache(arch, valid_funcs, embeddings)

        return len(valid_funcs)

    def rank_against_cached(self, source_code: str, arch: str,
                            top_k: int = 10) -> List[Tuple[Dict, float]]:
        """
        Rank cached functions by similarity to source code.

        Args:
            source_code: Source function text
            arch: Architecture to compare against
            top_k: Number of top results

        Returns:
            List of (function_dict, similarity) tuples
        """
        if arch not in self._disk_cache:
            if not self.load_cache(arch):
                return []

        # Encode source
        source_emb = self.encode(self._preprocess_code(source_code))

        # Get cached embeddings
        emb_matrix = self.get_cached_embeddings(arch)
        if emb_matrix is None:
            return []

        functions = self.get_cached_functions(arch)

        # Compute similarities
        similarities = np.dot(emb_matrix, source_emb)
        similarities = (similarities + 1) / 2  # Normalize to [0, 1]

        # Get top-k
        top_indices = np.argsort(similarities)[::-1][:top_k]

        return [(functions[i], float(similarities[i])) for i in top_indices]
