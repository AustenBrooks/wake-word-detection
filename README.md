# wake-word-detection
Repository containing the code to train a model to detect 2 words (right and bamboozle), silence, or unknown sounds and the code for deploying the model to an arducam Pico4ML embedded system


# micro_speech
micro_speech contains all the C++ code for deploying the model to the board, this is based off of the https://github.com/ArduCAM/pico-tflmicro.git, view the quick start guide at https://www.arducam.com/docs/pico/arducam-pico4mltinymldevkit/quick-pico-setup/#micro-speech for more instructions

# bamboozle
folder containing custom recordings of bamboozle

# bamboozle recorder
jupityr notebook for recording audio segments in the correct format to match the google dataset

# micro_speech.uf2
generated file to be uploaded to the board

# model training
jupityr notebook that uses the custom recordings and the google speech dataset (multiple variants available) to train and save a tensorflow model, also creates a tflite file which contains the hex values used in the micro_speech\micro_features\model.cpp file (this is how you upload a custom trained model)

# pitch shifting
jupityr notebook to pitch shift audio files (nice when using small amount of custom data)

# streaming audio
test script to run the model on a computer before deploying to a board
