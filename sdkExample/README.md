# Impact Subsea SDK example usage

Impact Subsea SDK example app written in C++17

## Compiling

### Using Microsoft Visual Studios with cmake
1. Open Microsoft Visual Studios and click `Continue without code`.
2. Open the `CmakeLists.txt` file from the menu `File->Open->CMake..`
3. Select the startup object from the dropdown green compile and run button.
4. Click the Compile / Run button.

### Linux, Windows using cmake

1. Make sure you have `cmake` installed.

2. Clone the git repo onto your local storage.

3. Change into root repo directory:

    ```
    $ cd sdkExample
    ```

4. Clone the git submodules.

    ```bash
    $ git submodule update --init --recursive --remote
    ```

5. Create a new build directory and change into it:

    ```bash
    $ mkdir build
    $ cd build
    ```

6. Run cmake on the parent directory to generate makefile:

    ```bash
    $ cmake -DCMAKE_BUILD_TYPE=Debug ..
    or
    $ cmake -DCMAKE_BUILD_TYPE=Release ..
    ```

7. Run make on the generated makefile to generate the static library:

    ```bash
    $ make
    ```
    If using windows
    ```bash
    $ cmake --build . --config Debug
    or
    $ cmake --build . --config Release
    ```

8. Run the exe in the folder sdkExample/build/bin/

- If on Linux make sure the app has permission to access the serial ports:

    ```bash
    $ sudo usermod -a -G dialout YOUR_USER_NAME
    ```