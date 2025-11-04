# Bark Detection Model Training Guide

Complete guide for training a bark detection model optimized for RTX 3090 GPU and deployment on ESP32.

## Table of Contents
1. [Overview](#overview)
2. [GPU Setup](#gpu-setup)
3. [Data Collection](#data-collection)
4. [Environment Setup](#environment-setup)
5. [Data Preparation](#data-preparation)
6. [Feature Extraction](#feature-extraction)
7. [Model Training](#model-training)
8. [Model Conversion](#model-conversion)
9. [Firmware Integration](#firmware-integration)
10. [Troubleshooting](#troubleshooting)
11. [Performance Targets](#performance-targets)

## Overview

This guide helps you train a 4-class audio classifier that runs on ESP32:
- **DOG_BARK** - Dog barking sounds
- **SPEECH** - Human speech
- **AMBIENT** - Background noise (traffic, music, etc.)
- **SILENCE** - Very low audio levels

**Key Specifications:**
- Input: 16kHz mono audio, 16-bit PCM
- Features: 40-band Mel spectrogram
- Model size: <50KB (optimized for ESP32)
- Inference time: <100ms per 1-second audio clip
- Expected accuracy: >90% on test set

## GPU Setup

### Requirements
- **GPU**: NVIDIA RTX 3090 or similar (24GB VRAM)
- **CUDA**: 11.8
- **cuDNN**: 8.6
- **Python**: 3.9 or 3.10
- **TensorFlow**: 2.12+ with GPU support

### CUDA 11.8 Installation (Ubuntu/Debian)

```bash
# Remove old CUDA installations
sudo apt-get --purge remove "*cuda*" "*cublas*" "*cufft*" "*cufile*" "*curand*" \
  "*cusolver*" "*cusparse*" "*gds-tools*" "*npp*" "*nvjpeg*" "nsight*" "*nvvm*"

# Install CUDA 11.8
wget https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/cuda-ubuntu2004.pin
sudo mv cuda-ubuntu2004.pin /etc/apt/preferences.d/cuda-repository-pin-600
wget https://developer.download.nvidia.com/compute/cuda/11.8.0/local_installers/cuda-repo-ubuntu2004-11-8-local_11.8.0-520.61.05-1_amd64.deb
sudo dpkg -i cuda-repo-ubuntu2004-11-8-local_11.8.0-520.61.05-1_amd64.deb
sudo cp /var/cuda-repo-ubuntu2004-11-8-local/cuda-*-keyring.gpg /usr/share/keyrings/
sudo apt-get update
sudo apt-get -y install cuda-11-8

# Set environment variables (add to ~/.bashrc)
export PATH=/usr/local/cuda-11.8/bin${PATH:+:${PATH}}
export LD_LIBRARY_PATH=/usr/local/cuda-11.8/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}
export CUDA_HOME=/usr/local/cuda-11.8
```

### cuDNN 8.6 Installation

1. Download cuDNN 8.6 for CUDA 11.x from [NVIDIA cuDNN page](https://developer.nvidia.com/cudnn)
2. Extract and copy files:

```bash
tar -xvf cudnn-linux-x86_64-8.6.0.163_cuda11-archive.tar.xz
sudo cp cudnn-*-archive/include/cudnn*.h /usr/local/cuda-11.8/include 
sudo cp -P cudnn-*-archive/lib/libcudnn* /usr/local/cuda-11.8/lib64 
sudo chmod a+r /usr/local/cuda-11.8/include/cudnn*.h /usr/local/cuda-11.8/lib64/libcudnn*
```

### Verify GPU Setup

Save and run this script:

```python
# verify_gpu.py
import tensorflow as tf

print("=" * 60)
print("GPU Verification Script")
print("=" * 60)

# Check TensorFlow version
print(f"\nTensorFlow version: {tf.__version__}")

# Check CUDA availability
print(f"CUDA available: {tf.test.is_built_with_cuda()}")
print(f"GPU available: {tf.test.is_gpu_available(cuda_only=False, min_cuda_compute_capability=None)}")

# List physical devices
print("\nPhysical devices:")
for device in tf.config.list_physical_devices():
    print(f"  {device}")

# List GPU devices specifically
gpus = tf.config.list_physical_devices('GPU')
print(f"\nNumber of GPUs: {len(gpus)}")

if gpus:
    for i, gpu in enumerate(gpus):
        print(f"\nGPU {i}: {gpu}")
        try:
            # Get GPU details
            gpu_details = tf.config.experimental.get_device_details(gpu)
            print(f"  Details: {gpu_details}")
        except:
            print("  Details not available")
        
        # Check memory
        try:
            tf.config.experimental.set_memory_growth(gpu, True)
            print("  Memory growth enabled")
        except RuntimeError as e:
            print(f"  Memory growth error: {e}")

# Test computation
print("\nTesting GPU computation...")
try:
    with tf.device('/GPU:0'):
        a = tf.constant([[1.0, 2.0], [3.0, 4.0]])
        b = tf.constant([[1.0, 1.0], [0.0, 1.0]])
        c = tf.matmul(a, b)
        print(f"Test matmul result:\n{c.numpy()}")
    print("✓ GPU computation successful!")
except Exception as e:
    print(f"✗ GPU computation failed: {e}")

print("\n" + "=" * 60)
```

Run: `python verify_gpu.py`

Expected output should show RTX 3090 detected with CUDA support.

## Data Collection

### Dataset Requirements

For good model performance, collect:
- **Dog Bark**: 2000+ clips (various breeds, distances)
- **Speech**: 2000+ clips (male/female, various languages)
- **Ambient**: 2000+ clips (traffic, music, nature, household)
- **Silence**: 1000+ clips (very quiet backgrounds)

**Clip specifications:**
- Duration: 1-5 seconds
- Format: WAV, MP3, or FLAC
- Sample rate: Any (will be resampled to 16kHz)
- Mono or stereo (will be converted to mono)

### Public Datasets

**Free datasets to get started:**

1. **ESC-50** (Environmental Sound Classification)
   - URL: https://github.com/karolpiczak/ESC-50
   - Contains: Dog barks, ambient sounds
   - License: Creative Commons

2. **UrbanSound8K**
   - URL: https://urbansounddataset.weill.cornell.edu/urbansound8k.html
   - Contains: Dog barks, street sounds, drilling
   - License: Academic use

3. **FSD50K** (Freesound Dataset)
   - URL: https://zenodo.org/record/4060432
   - Contains: Diverse sounds including dog barks
   - License: Creative Commons

4. **Common Voice** (Speech)
   - URL: https://commonvoice.mozilla.org/
   - Contains: Human speech in many languages
   - License: CC0

5. **AudioSet** (subset)
   - URL: https://research.google.com/audioset/
   - Contains: All categories
   - License: Creative Commons

### Directory Structure

Organize your dataset:
```
dataset/
├── raw/
│   ├── dog_bark/
│   │   ├── bark_001.wav
│   │   ├── bark_002.wav
│   │   └── ...
│   ├── speech/
│   │   ├── speech_001.wav
│   │   └── ...
│   ├── ambient/
│   │   ├── ambient_001.wav
│   │   └── ...
│   └── silence/
│       ├── silence_001.wav
│       └── ...
├── processed/
└── features/
```

## Environment Setup

### Create Python Environment

```bash
# Create virtual environment
python3.9 -m venv bark_training_env
source bark_training_env/bin/activate  # On Windows: bark_training_env\Scripts\activate

# Install dependencies
pip install --upgrade pip

# Install TensorFlow with GPU support
pip install tensorflow==2.12.0

# Install other dependencies
pip install numpy==1.23.5 \
            librosa==0.10.0 \
            scipy==1.10.1 \
            scikit-learn==1.2.2 \
            matplotlib==3.7.1 \
            pandas==2.0.1 \
            tqdm==4.65.0 \
            soundfile==0.12.1 \
            audioread==3.0.0

# For parallel processing
pip install joblib==1.2.0

# Optional: for tensorboard
pip install tensorboard==2.12.0
```

### GPU Configuration Script

Create `gpu_config.py`:

```python
# gpu_config.py
import tensorflow as tf
import os

def configure_gpu():
    """
    Configure GPU for optimal RTX 3090 performance
    """
    print("Configuring GPU...")
    
    # Set memory growth to prevent OOM errors
    gpus = tf.config.list_physical_devices('GPU')
    if gpus:
        try:
            for gpu in gpus:
                tf.config.experimental.set_memory_growth(gpu, True)
            print(f"✓ Memory growth enabled for {len(gpus)} GPU(s)")
        except RuntimeError as e:
            print(f"✗ Memory growth error: {e}")
    
    # Enable mixed precision (FP16) for faster training
    policy = tf.keras.mixed_precision.Policy('mixed_float16')
    tf.keras.mixed_precision.set_global_policy(policy)
    print(f"✓ Mixed precision policy set: {policy.name}")
    
    # Set TensorFlow log level
    os.environ['TF_CPP_MIN_LOG_LEVEL'] = '2'
    
    # Enable XLA (Accelerated Linear Algebra)
    tf.config.optimizer.set_jit(True)
    print("✓ XLA JIT compilation enabled")
    
    # Allow TensorFlow to allocate only as much GPU memory as needed
    # Uncomment if you want to set a specific memory limit (in MB)
    # tf.config.set_logical_device_configuration(
    #     gpus[0],
    #     [tf.config.LogicalDeviceConfiguration(memory_limit=20480)])
    
    return gpus

if __name__ == "__main__":
    gpus = configure_gpu()
    print(f"\nGPU configuration complete. {len(gpus)} GPU(s) available.")
```

## Data Preparation

### GPU-Accelerated Data Preprocessing

Create `prepare_dataset_gpu.py`:

```python
# prepare_dataset_gpu.py
import os
import numpy as np
import librosa
import soundfile as sf
from pathlib import Path
from tqdm import tqdm
from joblib import Parallel, delayed
import argparse

# Audio parameters matching firmware
SAMPLE_RATE = 16000
TARGET_DURATION = 1.0  # seconds
TARGET_SAMPLES = int(SAMPLE_RATE * TARGET_DURATION)

class AudioPreprocessor:
    def __init__(self, sample_rate=SAMPLE_RATE):
        self.sample_rate = sample_rate
    
    def load_audio(self, file_path):
        """Load and preprocess audio file"""
        try:
            # Load audio (librosa automatically converts to mono)
            audio, sr = librosa.load(file_path, sr=self.sample_rate, mono=True)
            return audio, sr
        except Exception as e:
            print(f"Error loading {file_path}: {e}")
            return None, None
    
    def normalize_audio(self, audio):
        """Normalize audio to [-1, 1] range"""
        if len(audio) == 0:
            return audio
        max_val = np.abs(audio).max()
        if max_val > 0:
            audio = audio / max_val
        return audio
    
    def pad_or_trim(self, audio, target_length):
        """Pad or trim audio to target length"""
        if len(audio) < target_length:
            # Pad with zeros
            padding = target_length - len(audio)
            audio = np.pad(audio, (0, padding), mode='constant')
        elif len(audio) > target_length:
            # Trim to target length
            audio = audio[:target_length]
        return audio
    
    def augment_audio(self, audio, augment_type=None):
        """Apply data augmentation"""
        if augment_type is None:
            return audio
        
        if augment_type == 'pitch_shift':
            # Shift pitch by random semitones
            n_steps = np.random.randint(-2, 3)
            audio = librosa.effects.pitch_shift(audio, sr=self.sample_rate, n_steps=n_steps)
        
        elif augment_type == 'time_stretch':
            # Stretch time by random factor
            rate = np.random.uniform(0.9, 1.1)
            audio = librosa.effects.time_stretch(audio, rate=rate)
        
        elif augment_type == 'add_noise':
            # Add Gaussian noise
            noise_level = np.random.uniform(0.001, 0.005)
            noise = np.random.normal(0, noise_level, len(audio))
            audio = audio + noise
        
        elif augment_type == 'gain':
            # Adjust gain
            gain = np.random.uniform(0.7, 1.3)
            audio = audio * gain
        
        return audio
    
    def process_file(self, input_path, output_path, augment=False):
        """Process a single audio file"""
        # Load audio
        audio, sr = self.load_audio(input_path)
        if audio is None:
            return False
        
        # Normalize
        audio = self.normalize_audio(audio)
        
        # Pad or trim
        audio = self.pad_or_trim(audio, TARGET_SAMPLES)
        
        # Save processed audio
        sf.write(output_path, audio, self.sample_rate)
        
        # Create augmented versions if requested
        if augment:
            base_name = output_path.stem
            parent_dir = output_path.parent
            
            for aug_type in ['pitch_shift', 'time_stretch', 'add_noise', 'gain']:
                aug_audio = self.augment_audio(audio.copy(), aug_type)
                aug_audio = self.normalize_audio(aug_audio)
                aug_audio = self.pad_or_trim(aug_audio, TARGET_SAMPLES)
                
                aug_path = parent_dir / f"{base_name}_{aug_type}.wav"
                sf.write(aug_path, aug_audio, self.sample_rate)
        
        return True

def process_dataset(input_dir, output_dir, augment=False, n_jobs=-1):
    """Process entire dataset in parallel"""
    input_path = Path(input_dir)
    output_path = Path(output_dir)
    
    preprocessor = AudioPreprocessor()
    
    # Find all audio files
    audio_extensions = ['.wav', '.mp3', '.flac', '.ogg', '.m4a']
    
    tasks = []
    for class_dir in input_path.iterdir():
        if not class_dir.is_dir():
            continue
        
        # Create output directory for class
        class_output_dir = output_path / class_dir.name
        class_output_dir.mkdir(parents=True, exist_ok=True)
        
        # Find all audio files in class directory
        audio_files = []
        for ext in audio_extensions:
            audio_files.extend(class_dir.glob(f"*{ext}"))
        
        # Create task for each file
        for audio_file in audio_files:
            output_file = class_output_dir / f"{audio_file.stem}.wav"
            tasks.append((audio_file, output_file, augment))
        
        print(f"Found {len(audio_files)} files in {class_dir.name}")
    
    print(f"\nProcessing {len(tasks)} files with {n_jobs} parallel jobs...")
    print(f"Augmentation: {'enabled' if augment else 'disabled'}")
    
    # Process in parallel
    results = Parallel(n_jobs=n_jobs)(
        delayed(preprocessor.process_file)(input_f, output_f, aug)
        for input_f, output_f, aug in tqdm(tasks)
    )
    
    successful = sum(results)
    print(f"\n✓ Successfully processed {successful}/{len(tasks)} files")
    
    return successful

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Preprocess audio dataset for bark detection')
    parser.add_argument('--input', type=str, default='dataset/raw',
                       help='Input directory with raw audio files')
    parser.add_argument('--output', type=str, default='dataset/processed',
                       help='Output directory for processed files')
    parser.add_argument('--augment', action='store_true',
                       help='Enable data augmentation')
    parser.add_argument('--jobs', type=int, default=-1,
                       help='Number of parallel jobs (-1 for all cores)')
    
    args = parser.parse_args()
    
    process_dataset(args.input, args.output, args.augment, args.jobs)
```

Run preprocessing:
```bash
# Without augmentation
python prepare_dataset_gpu.py --input dataset/raw --output dataset/processed

# With augmentation (5x more data)
python prepare_dataset_gpu.py --input dataset/raw --output dataset/processed --augment
```

## Feature Extraction

### GPU-Accelerated Feature Extraction

Create `extract_features_gpu.py`:

```python
# extract_features_gpu.py
import os
import numpy as np
import librosa
from pathlib import Path
from tqdm import tqdm
from joblib import Parallel, delayed
import argparse

# Feature parameters matching firmware
SAMPLE_RATE = 16000
N_FFT = 512
HOP_LENGTH = 256
N_MELS = 40
N_TIME_FRAMES = 32

class FeatureExtractor:
    def __init__(self, sample_rate=SAMPLE_RATE, n_fft=N_FFT, 
                 hop_length=HOP_LENGTH, n_mels=N_MELS):
        self.sample_rate = sample_rate
        self.n_fft = n_fft
        self.hop_length = hop_length
        self.n_mels = n_mels
    
    def extract_mel_spectrogram(self, audio):
        """
        Extract mel spectrogram features matching firmware implementation
        """
        # Compute mel spectrogram
        mel_spec = librosa.feature.melspectrogram(
            y=audio,
            sr=self.sample_rate,
            n_fft=self.n_fft,
            hop_length=self.hop_length,
            n_mels=self.n_mels,
            fmin=0,
            fmax=self.sample_rate / 2.0,
            window='hamming'
        )
        
        # Convert to log scale (dB)
        mel_spec_db = librosa.power_to_db(mel_spec, ref=np.max)
        
        # Ensure we have exactly N_TIME_FRAMES
        if mel_spec_db.shape[1] < N_TIME_FRAMES:
            # Pad with minimum value
            pad_width = N_TIME_FRAMES - mel_spec_db.shape[1]
            mel_spec_db = np.pad(mel_spec_db, ((0, 0), (0, pad_width)), 
                                mode='constant', constant_values=mel_spec_db.min())
        elif mel_spec_db.shape[1] > N_TIME_FRAMES:
            # Trim to target size
            mel_spec_db = mel_spec_db[:, :N_TIME_FRAMES]
        
        # Transpose to [time, mels] format
        mel_spec_db = mel_spec_db.T
        
        return mel_spec_db
    
    def process_file(self, audio_path, class_label):
        """Process single audio file and extract features"""
        try:
            # Load audio
            audio, sr = librosa.load(audio_path, sr=self.sample_rate, mono=True)
            
            # Extract features
            features = self.extract_mel_spectrogram(audio)
            
            return features, class_label
        except Exception as e:
            print(f"Error processing {audio_path}: {e}")
            return None, None

def extract_dataset_features(processed_dir, output_dir, n_jobs=-1):
    """Extract features from entire dataset"""
    processed_path = Path(processed_dir)
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)
    
    extractor = FeatureExtractor()
    
    # Class mapping
    class_names = ['dog_bark', 'speech', 'ambient', 'silence']
    class_to_idx = {name: idx for idx, name in enumerate(class_names)}
    
    # Collect all audio files
    tasks = []
    for class_dir in processed_path.iterdir():
        if not class_dir.is_dir():
            continue
        
        class_name = class_dir.name
        if class_name not in class_to_idx:
            print(f"Warning: Unknown class '{class_name}', skipping...")
            continue
        
        class_label = class_to_idx[class_name]
        
        audio_files = list(class_dir.glob("*.wav"))
        for audio_file in audio_files:
            tasks.append((audio_file, class_label))
        
        print(f"Found {len(audio_files)} files in {class_name} (label={class_label})")
    
    print(f"\nExtracting features from {len(tasks)} files...")
    
    # Extract features in parallel
    results = Parallel(n_jobs=n_jobs)(
        delayed(extractor.process_file)(audio_path, label)
        for audio_path, label in tqdm(tasks)
    )
    
    # Filter out failed extractions
    features_list = []
    labels_list = []
    for features, label in results:
        if features is not None:
            features_list.append(features)
            labels_list.append(label)
    
    # Convert to numpy arrays
    X = np.array(features_list, dtype=np.float32)
    y = np.array(labels_list, dtype=np.int32)
    
    print(f"\n✓ Extracted features: X.shape={X.shape}, y.shape={y.shape}")
    print(f"  Features per sample: {N_TIME_FRAMES} time frames × {N_MELS} mel bands")
    
    # Save features
    np.save(output_path / 'features.npy', X)
    np.save(output_path / 'labels.npy', y)
    np.save(output_path / 'class_names.npy', np.array(class_names))
    
    print(f"\n✓ Saved features to {output_path}")
    
    # Print class distribution
    print("\nClass distribution:")
    for i, name in enumerate(class_names):
        count = np.sum(y == i)
        print(f"  {name}: {count} samples ({count/len(y)*100:.1f}%)")
    
    return X, y

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Extract features from processed audio')
    parser.add_argument('--input', type=str, default='dataset/processed',
                       help='Input directory with processed audio files')
    parser.add_argument('--output', type=str, default='dataset/features',
                       help='Output directory for extracted features')
    parser.add_argument('--jobs', type=int, default=-1,
                       help='Number of parallel jobs (-1 for all cores)')
    
    args = parser.parse_args()
    
    extract_dataset_features(args.input, args.output, args.jobs)
```

Run feature extraction:
```bash
python extract_features_gpu.py --input dataset/processed --output dataset/features
```

## Model Training

### GPU-Optimized Training Script

Create `train_model_gpu.py`:

```python
# train_model_gpu.py
import numpy as np
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import LabelBinarizer
import argparse
from pathlib import Path
import json
from datetime import datetime

# Import GPU configuration
from gpu_config import configure_gpu

# Model parameters
N_MELS = 40
N_TIME_FRAMES = 32
N_CLASSES = 4

def create_model(input_shape=(N_TIME_FRAMES, N_MELS, 1), num_classes=N_CLASSES):
    """
    Create CNN model optimized for ESP32 deployment
    - Small model size (<50KB)
    - Fast inference (<100ms)
    - High accuracy (>90%)
    """
    model = keras.Sequential([
        # Input layer
        layers.Input(shape=input_shape),
        
        # First convolutional block
        layers.Conv2D(16, (3, 3), activation='relu', padding='same'),
        layers.MaxPooling2D((2, 2)),
        layers.Dropout(0.2),
        
        # Second convolutional block
        layers.Conv2D(32, (3, 3), activation='relu', padding='same'),
        layers.MaxPooling2D((2, 2)),
        layers.Dropout(0.2),
        
        # Third convolutional block
        layers.Conv2D(32, (3, 3), activation='relu', padding='same'),
        layers.MaxPooling2D((2, 2)),
        layers.Dropout(0.2),
        
        # Flatten and dense layers
        layers.Flatten(),
        layers.Dense(64, activation='relu'),
        layers.Dropout(0.3),
        layers.Dense(num_classes, activation='softmax', dtype='float32')  # Force float32 output
    ])
    
    return model

class TrainingCallback(keras.callbacks.Callback):
    """Custom callback for training monitoring"""
    def on_epoch_end(self, epoch, logs=None):
        if logs:
            print(f"\nEpoch {epoch + 1}:")
            print(f"  Train Loss: {logs['loss']:.4f}, Train Acc: {logs['accuracy']:.4f}")
            print(f"  Val Loss: {logs['val_loss']:.4f}, Val Acc: {logs['val_accuracy']:.4f}")

def train_model(features_dir, output_dir, batch_size=512, epochs=100, 
                validation_split=0.2, learning_rate=0.001):
    """Train bark detection model with GPU optimization"""
    
    # Configure GPU
    gpus = configure_gpu()
    print(f"\n✓ Using {len(gpus)} GPU(s) for training\n")
    
    # Load features
    features_path = Path(features_dir)
    X = np.load(features_path / 'features.npy')
    y = np.load(features_path / 'labels.npy')
    class_names = np.load(features_path / 'class_names.npy')
    
    print(f"Loaded dataset: X.shape={X.shape}, y.shape={y.shape}")
    print(f"Classes: {class_names}")
    
    # Reshape for CNN: add channel dimension
    X = X.reshape(-1, N_TIME_FRAMES, N_MELS, 1)
    
    # One-hot encode labels
    lb = LabelBinarizer()
    y_onehot = lb.fit_transform(y)
    
    # Split dataset
    X_train, X_test, y_train, y_test = train_test_split(
        X, y_onehot, test_size=validation_split, random_state=42, stratify=y
    )
    
    print(f"\nTrain set: {X_train.shape[0]} samples")
    print(f"Test set: {X_test.shape[0]} samples")
    
    # Create model
    print("\nBuilding model...")
    model = create_model()
    model.summary()
    
    # Compile with mixed precision-compatible optimizer
    optimizer = keras.optimizers.Adam(learning_rate=learning_rate)
    
    # Wrap optimizer for mixed precision if using FP16
    if tf.keras.mixed_precision.global_policy().name == 'mixed_float16':
        optimizer = keras.mixed_precision.LossScaleOptimizer(optimizer)
    
    model.compile(
        optimizer=optimizer,
        loss='categorical_crossentropy',
        metrics=['accuracy']
    )
    
    # Prepare callbacks
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)
    
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_dir = output_path / f"logs/{timestamp}"
    
    callbacks = [
        keras.callbacks.ModelCheckpoint(
            filepath=str(output_path / 'best_model.h5'),
            save_best_only=True,
            monitor='val_accuracy',
            mode='max',
            verbose=1
        ),
        keras.callbacks.EarlyStopping(
            monitor='val_loss',
            patience=15,
            restore_best_weights=True,
            verbose=1
        ),
        keras.callbacks.ReduceLROnPlateau(
            monitor='val_loss',
            factor=0.5,
            patience=5,
            min_lr=1e-7,
            verbose=1
        ),
        keras.callbacks.TensorBoard(
            log_dir=str(log_dir),
            histogram_freq=1,
            write_graph=True,
            update_freq='epoch'
        ),
        TrainingCallback()
    ]
    
    # Train model
    print(f"\nStarting training with batch_size={batch_size}...")
    print(f"Expected speed: 50K-100K samples/second on RTX 3090")
    print(f"TensorBoard logs: {log_dir}")
    print(f"\nTo monitor training, run: tensorboard --logdir {output_path / 'logs'}\n")
    
    history = model.fit(
        X_train, y_train,
        batch_size=batch_size,
        epochs=epochs,
        validation_data=(X_test, y_test),
        callbacks=callbacks,
        verbose=2
    )
    
    # Evaluate final model
    print("\n" + "=" * 60)
    print("Final Evaluation")
    print("=" * 60)
    
    test_loss, test_acc = model.evaluate(X_test, y_test, verbose=0)
    print(f"Test Loss: {test_loss:.4f}")
    print(f"Test Accuracy: {test_acc:.4f} ({test_acc*100:.2f}%)")
    
    # Save final model
    model.save(output_path / 'final_model.h5')
    print(f"\n✓ Model saved to {output_path}")
    
    # Save training history
    history_dict = {
        'loss': [float(x) for x in history.history['loss']],
        'accuracy': [float(x) for x in history.history['accuracy']],
        'val_loss': [float(x) for x in history.history['val_loss']],
        'val_accuracy': [float(x) for x in history.history['val_accuracy']],
        'test_loss': float(test_loss),
        'test_accuracy': float(test_acc)
    }
    
    with open(output_path / 'training_history.json', 'w') as f:
        json.dump(history_dict, f, indent=2)
    
    # Save class names
    with open(output_path / 'class_names.json', 'w') as f:
        json.dump(class_names.tolist(), f, indent=2)
    
    print("\n" + "=" * 60)
    print("Training Complete!")
    print("=" * 60)
    print(f"Best model: {output_path / 'best_model.h5'}")
    print(f"Final model: {output_path / 'final_model.h5'}")
    print(f"Next step: Run convert_to_tflite.py to convert for ESP32")
    
    return model, history

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Train bark detection model on GPU')
    parser.add_argument('--features', type=str, default='dataset/features',
                       help='Directory containing extracted features')
    parser.add_argument('--output', type=str, default='models',
                       help='Output directory for trained models')
    parser.add_argument('--batch-size', type=int, default=512,
                       help='Batch size (512-2048 for RTX 3090)')
    parser.add_argument('--epochs', type=int, default=100,
                       help='Number of training epochs')
    parser.add_argument('--lr', type=float, default=0.001,
                       help='Learning rate')
    
    args = parser.parse_args()
    
    train_model(
        features_dir=args.features,
        output_dir=args.output,
        batch_size=args.batch_size,
        epochs=args.epochs,
        learning_rate=args.lr
    )
```

Run training:
```bash
# Standard training
python train_model_gpu.py --features dataset/features --output models

# High-performance training (RTX 3090)
python train_model_gpu.py --features dataset/features --output models --batch-size 2048

# Monitor with TensorBoard (in separate terminal)
tensorboard --logdir models/logs
```

## Model Conversion

### TFLite Conversion and C Array Generation

Create `convert_to_tflite.py`:

```python
# convert_to_tflite.py
import tensorflow as tf
import numpy as np
from pathlib import Path
import argparse

def convert_to_tflite(model_path, output_dir, quantize=True):
    """
    Convert Keras model to TFLite format with INT8 quantization
    """
    print(f"Converting model: {model_path}")
    
    # Load model
    model = tf.keras.models.load_model(model_path)
    
    # Create converter
    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    
    if quantize:
        print("Enabling INT8 quantization...")
        
        # Load sample data for representative dataset
        features_path = Path('dataset/features')
        if (features_path / 'features.npy').exists():
            X = np.load(features_path / 'features.npy')
            # Reshape for model input
            X = X.reshape(-1, 32, 40, 1).astype(np.float32)
            
            # Use first 100 samples as representative dataset
            representative_data = X[:100]
            
            def representative_dataset():
                for sample in representative_data:
                    yield [sample.reshape(1, 32, 40, 1)]
            
            converter.representative_dataset = representative_dataset
            converter.optimizations = [tf.lite.Optimize.DEFAULT]
            converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
            converter.inference_input_type = tf.int8
            converter.inference_output_type = tf.int8
        else:
            print("Warning: No representative dataset found, using default quantization")
            converter.optimizations = [tf.lite.Optimize.DEFAULT]
    
    # Convert
    tflite_model = converter.convert()
    
    # Save .tflite file
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)
    
    tflite_path = output_path / 'bark_detector.tflite'
    with open(tflite_path, 'wb') as f:
        f.write(tflite_model)
    
    model_size = len(tflite_model) / 1024
    print(f"✓ TFLite model saved: {tflite_path}")
    print(f"  Model size: {model_size:.2f} KB")
    
    # Generate C array header file
    generate_c_header(tflite_model, output_path / 'model_data.h')
    
    return tflite_model

def generate_c_header(tflite_model, output_path):
    """
    Generate C header file with model data as byte array
    """
    print(f"\nGenerating C header file: {output_path}")
    
    model_bytes = bytes(tflite_model)
    model_len = len(model_bytes)
    
    with open(output_path, 'w') as f:
        f.write("#ifndef MODEL_DATA_H\n")
        f.write("#define MODEL_DATA_H\n\n")
        f.write("// Auto-generated model data\n")
        f.write(f"// Model size: {model_len} bytes ({model_len/1024:.2f} KB)\n")
        f.write("// Generated by convert_to_tflite.py\n\n")
        f.write("#define BARK_DETECTOR_MODEL_DATA_AVAILABLE\n\n")
        f.write("// Align model data to 16-byte boundary for optimal performance\n")
        f.write("alignas(16) const unsigned char g_model_data[] = {\n")
        
        # Write bytes in rows of 12
        for i in range(0, model_len, 12):
            chunk = model_bytes[i:i+12]
            hex_str = ', '.join(f'0x{b:02x}' for b in chunk)
            f.write(f"  {hex_str},\n")
        
        f.write("};\n\n")
        f.write(f"const int g_model_data_len = {model_len};\n\n")
        f.write("#endif // MODEL_DATA_H\n")
    
    print(f"✓ C header file generated: {output_path}")
    print(f"  Array name: g_model_data")
    print(f"  Array length: g_model_data_len = {model_len}")
    print(f"\nNext step: Copy {output_path.name} to components/bark_detector/src/")

def test_tflite_model(tflite_path, features_path):
    """
    Test the TFLite model to ensure it works correctly
    """
    print(f"\nTesting TFLite model: {tflite_path}")
    
    # Load model
    interpreter = tf.lite.Interpreter(model_path=str(tflite_path))
    interpreter.allocate_tensors()
    
    # Get input and output details
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    
    print(f"Input shape: {input_details[0]['shape']}")
    print(f"Input dtype: {input_details[0]['dtype']}")
    print(f"Output shape: {output_details[0]['shape']}")
    print(f"Output dtype: {output_details[0]['dtype']}")
    
    # Load test data
    features_dir = Path(features_path)
    if (features_dir / 'features.npy').exists():
        X = np.load(features_dir / 'features.npy')
        y = np.load(features_dir / 'labels.npy')
        class_names = np.load(features_dir / 'class_names.npy')
        
        # Test on first sample
        test_sample = X[0].reshape(1, 32, 40, 1).astype(np.float32)
        
        # Handle quantization
        if input_details[0]['dtype'] == np.int8:
            # Quantize input
            input_scale, input_zero_point = input_details[0]['quantization']
            test_sample = test_sample / input_scale + input_zero_point
            test_sample = test_sample.astype(np.int8)
        
        # Run inference
        interpreter.set_tensor(input_details[0]['index'], test_sample)
        interpreter.invoke()
        output = interpreter.get_tensor(output_details[0]['index'])
        
        # Handle quantized output
        if output_details[0]['dtype'] == np.int8:
            output_scale, output_zero_point = output_details[0]['quantization']
            output = (output.astype(np.float32) - output_zero_point) * output_scale
        
        predicted_class = np.argmax(output[0])
        confidence = output[0][predicted_class]
        
        print(f"\nTest inference:")
        print(f"  True label: {class_names[y[0]]} ({y[0]})")
        print(f"  Predicted: {class_names[predicted_class]} ({predicted_class})")
        print(f"  Confidence: {confidence:.4f}")
        print(f"  All confidences: {output[0]}")
        
        print("\n✓ TFLite model test passed!")
    else:
        print("Warning: No test data available")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Convert Keras model to TFLite and C header')
    parser.add_argument('--model', type=str, default='models/best_model.h5',
                       help='Path to trained Keras model')
    parser.add_argument('--output', type=str, default='models/tflite',
                       help='Output directory for TFLite model')
    parser.add_argument('--features', type=str, default='dataset/features',
                       help='Features directory for testing')
    parser.add_argument('--no-quantize', action='store_true',
                       help='Disable INT8 quantization')
    
    args = parser.parse_args()
    
    # Convert model
    tflite_model = convert_to_tflite(
        model_path=args.model,
        output_dir=args.output,
        quantize=not args.no_quantize
    )
    
    # Test model
    tflite_path = Path(args.output) / 'bark_detector.tflite'
    test_tflite_model(tflite_path, args.features)
    
    print("\n" + "=" * 60)
    print("Conversion Complete!")
    print("=" * 60)
    print(f"TFLite model: {tflite_path}")
    print(f"C header: {Path(args.output) / 'model_data.h'}")
```

Run conversion:
```bash
python convert_to_tflite.py --model models/best_model.h5 --output models/tflite
```

## Firmware Integration

### Steps to Integrate Trained Model

1. **Copy model_data.h to firmware:**
```bash
cp models/tflite/model_data.h components/bark_detector/src/
```

2. **Verify the file has required definitions:**
   - Should contain `g_model_data[]` array
   - Should contain `g_model_data_len` variable
   - Should have `#define BARK_DETECTOR_MODEL_DATA_AVAILABLE`

3. **Build firmware:**
```bash
# Using PlatformIO
pio run

# Or using Arduino IDE/CLI
arduino-cli compile --fqbn esp32:esp32:esp32 .
```

4. **Flash to ESP32:**
```bash
# Using PlatformIO
pio run --target upload

# Or using Arduino IDE/CLI  
arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 .
```

5. **Monitor serial output:**
```bash
pio device monitor --baud 115200
```

### Example Usage in Firmware

```cpp
#include "components/bark_detector/include/bark_detector_api.h"

// Create detector
BarkDetector detector;

// Configure
BarkDetectorConfig config;
config.confidence_threshold = 0.75f;
config.min_bark_duration_ms = 300;
config.enable_temporal_filter = true;

// Initialize
if (detector.initialize(config)) {
    Serial.println("Bark detector initialized successfully!");
} else {
    Serial.println("Failed to initialize detector");
}

// Process audio in main loop
void loop() {
    // Get audio samples from I2S microphone
    int16_t audio_buffer[16000]; // 1 second at 16kHz
    size_t samples_read = read_i2s_audio(audio_buffer, 16000);
    
    // Detect barks
    DetectionResult result = detector.process(audio_buffer, samples_read);
    
    if (result.is_bark) {
        Serial.printf("BARK DETECTED! Confidence: %.2f\n", result.confidence);
    }
    
    // Print all results
    Serial.printf("Class: %s, Confidence: %.2f\n",
                  bark_class_to_string(result.detected_class),
                  result.confidence);
}
```

## Troubleshooting

### GPU Issues

**Problem: GPU not detected**
```bash
# Check NVIDIA driver
nvidia-smi

# Check CUDA installation
nvcc --version

# Reinstall TensorFlow GPU
pip uninstall tensorflow
pip install tensorflow==2.12.0
```

**Problem: Out of memory errors**
- Reduce batch size: Try 256, 512, 1024
- Enable memory growth in `gpu_config.py`
- Close other GPU applications

**Problem: Slow training**
- Verify mixed precision is enabled
- Check GPU utilization: `nvidia-smi dmon`
- Increase batch size if memory allows
- Ensure data is not bottleneck (use profiler)

### Model Quality Issues

**Problem: Low accuracy (<85%)**
- Collect more diverse training data
- Enable data augmentation
- Train for more epochs
- Check class balance
- Verify feature extraction matches firmware

**Problem: High false positive rate**
- Increase confidence threshold (0.8-0.9)
- Enable temporal filtering
- Add more negative examples to training set
- Review noise gate threshold

**Problem: Doesn't detect distant barks**
- Adjust AGC target level (increase)
- Lower noise gate threshold (-50 dB)
- Add more training data at various distances
- Check microphone gain settings

### Deployment Issues

**Problem: Model too large**
- Enable INT8 quantization
- Reduce model complexity (fewer filters/layers)
- Check tensor arena size

**Problem: Inference too slow**
- Use quantized model (INT8)
- Reduce input size (fewer time frames)
- Check tensor arena is large enough
- Profile with `get_stats()`

**Problem: Build errors**
- Install TensorFlow Lite Micro for ESP32
- Check component paths in build system
- Verify model_data.h is properly formatted
- Check for missing dependencies

## Performance Targets

### Training Performance (RTX 3090)

| Metric | Target | Notes |
|--------|--------|-------|
| Training speed | 50K-100K samples/sec | With batch size 1024-2048 |
| Epoch time | 10-30 seconds | For 10K samples |
| GPU utilization | >80% | Check with `nvidia-smi` |
| Memory usage | 8-16 GB | Out of 24 GB available |

### Model Performance

| Metric | Target | Notes |
|--------|--------|-------|
| Accuracy | >90% | On balanced test set |
| Precision (bark) | >85% | Reduce false positives |
| Recall (bark) | >90% | Catch most barks |
| Model size | <50 KB | After INT8 quantization |
| F1 score | >0.88 | Balanced metric |

### Inference Performance (ESP32)

| Metric | Target | Notes |
|--------|--------|-------|
| Inference time | <100 ms | Per 1-second clip |
| Memory usage | <64 KB | Tensor arena |
| CPU usage | <30% | Leave room for other tasks |
| Latency | <200 ms | End-to-end detection |

### Data Requirements

| Category | Minimum | Recommended | Notes |
|----------|---------|-------------|-------|
| Dog barks | 1000 | 3000+ | Various breeds, distances |
| Speech | 1000 | 3000+ | Male/female, languages |
| Ambient | 1000 | 2000+ | Diverse environments |
| Silence | 500 | 1000+ | Quiet backgrounds |
| **Total** | **3500** | **9000+** | More data = better model |

## Quick Reference

### Complete Training Pipeline

```bash
# 1. Setup environment
python verify_gpu.py

# 2. Prepare dataset
python prepare_dataset_gpu.py --input dataset/raw --output dataset/processed --augment

# 3. Extract features
python extract_features_gpu.py --input dataset/processed --output dataset/features

# 4. Train model
python train_model_gpu.py --features dataset/features --output models --batch-size 1024

# 5. Convert to TFLite
python convert_to_tflite.py --model models/best_model.h5 --output models/tflite

# 6. Copy to firmware
cp models/tflite/model_data.h components/bark_detector/src/

# 7. Build and flash
pio run --target upload
```

### Monitoring Training

```bash
# Terminal 1: Training
python train_model_gpu.py

# Terminal 2: TensorBoard
tensorboard --logdir models/logs

# Terminal 3: GPU monitoring
watch -n 1 nvidia-smi
```

## Additional Resources

- [TensorFlow Lite Micro Documentation](https://www.tensorflow.org/lite/microcontrollers)
- [ESP32 I2S Audio](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/i2s.html)
- [AudioSet Dataset](https://research.google.com/audioset/)
- [Librosa Documentation](https://librosa.org/doc/latest/index.html)

---

**Questions or issues?** Open an issue on GitHub with:
- Your GPU model and CUDA version
- Training logs and error messages
- Model accuracy and confusion matrix
- Inference performance measurements
