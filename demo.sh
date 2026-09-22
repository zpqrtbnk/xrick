

case "$1" in
    record)
        shift
        xrick/bin/Release/xrick.exe -record demo.c -submap "$@"
        ;;
    diff)
        shift
        '/c/Program\ Files/Beyond\ Compare\ 5/BComp.exe' demo.c xrick/src/dat_demo.c
        ;;
    build)
        shift
        '/c/Program Files/Microsoft Visual Studio/18/Insiders/MSBuild/Current/Bin/MSBuild.exe' xrick.sln -p:Configuration=Release -p:Platform=x64 -t:Build
        ;;
    play)
        shift
        xrick/bin/Release/xrick.exe -demo -submap "$@"
        ;;
    *)
        echo "usage: demo.sh record|diff|build|play [submap]"
        exit 1
        ;;
esac
