#!/bin/bash


# download dependencies if they aren't already present
if [ ! -f "org.alloytools.alloy.dist.jar" ]; then
    wget https://github.com/AlloyTools/org.alloytools.alloy/releases/download/v6.0.0/org.alloytools.alloy.dist.jar
fi
if [ ! -f "json.jar" ]; then
    wget https://search.maven.org/remotecontent?filepath=org/json/json/20220924/json-20220924.jar -O json.jar
fi

# TODO: make using ZGC dependent on arguments to this script or what Java program the user is running
java -cp org.alloytools.alloy.dist.jar:json.jar "$@"
