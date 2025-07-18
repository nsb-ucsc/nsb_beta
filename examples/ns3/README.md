### **Installing ns-3** 

Here we cover the detailed steps for integrating NSB with the ns-3
simulator. The first step is to install ns-3. The official
[website]{.underline}(https://www.nsnam.org/docs/release/3.41/tutorial/html/getting-started.html)
provides detailed information and different installation methods. For
simpler installation, you can [download using
git]{.underline}(https://www.nsnam.org/docs/release/3.41/tutorial/html/getting-started.html#downloading-ns-3-using-git).

Once you have the environment for ns-3 setup, and have run the
instructions on
[testing]{.underline}(https://www.nsnam.org/docs/tutorial/html/getting-started.html#testing-ns-3)
, you are all set to get started with the NSB integration!

All newly created scripts can be placed under the ***scratch*** folder
and can be run using the command from the top of the ns-3 filesystem.
(for example the ***ns-3-dev***)


```bash
./ns3 run scratch/<your_file_name>
```

In case you do not want to run ***build*** every time you run a program,
you can add this option


```bash
./ns3 run scratch/<your_file_name> --no-build
```

### **Linking NSB with ns-3 through the CMakeLists.txt** 

Ns-3 contains a top-level CMakeLists.txt provided under its root
directory (for example the ***ns-3-dev***). In this, we need to add the
following to find the pkfg-config for NSB and include headers and make
it available globally. This can be added after the ***process_options(
)*** part

```bash
find_package(PkgConfig REQUIRED)
pkg_check_modules(NSB REQUIRED nsb)

set(EXTERNAL_LIBS ${nsb})
set(EXTERNAL_INCLUDE_DIRS ${nsb})
set(EXTERNAL_CFLAGS ${nsb})

include_directories(${NSB_INCLUDE_DIRS})
link_directories(${NSB_LIBRARY_DIRS})

set(NSB_LINK_LIBS${NSB_LIBRARIES})
```

Next, we need to link the libraries with the files in the scratch
folder, where we will be adding our custom scripts. This needs to be
done in CMakeLists.txt found under ***scratch/.***

First, under the ***build_exec*** function, we also need to add the
library linking for NSB, so it looks like this

```bash
build_exec(
EXECNAME ${scratch_name}

EXECNAME_PREFIX ${target_prefix}

SOURCE_FILES "${source_files}"

LIBRARIES_TO_LINK "${ns3-libs}" "${ns3-contrib-libs}"
"${NSB_LIBRARIES}"

EXECUTABLE_DIRECTORY_PATH ${scratch_directory}
)
```

Additionally, before the ***create_scratch*** , we need to add this

include_directories(\${NSB_INCLUDE_DIRS})

link_directories(\${NSB_LIBRARY_DIRS})

### **Testing with a simple AppClient script** 

Next, move the example code provided under ns3 folder, called
***ns3Simple-testing.cc*** to your scratch folder. Like with other
simulators, make sure you have the RedisDB running, NSB Daemon running
and then run this script using

```bash
./ns3 run scratch/ns3Simple-testing.cc
```

**Note**

In ns-3 given that it is a top-down network simulator, we will set
system-wide mode in the config.yaml.
ns3 documentation coming soon
