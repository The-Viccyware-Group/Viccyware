works with latest protoc + protoc-gen-go

rm -rf out && mkdir out && protoc --proto_path=./ --go_out=out --go_opt=paths=source_relative *.proto
