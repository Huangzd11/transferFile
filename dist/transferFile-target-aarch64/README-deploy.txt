目标机部署说明
1. 解压到 /opt/transfer（或任意目录）
2. export LD_LIBRARY_PATH=/opt/transfer/lib:$LD_LIBRARY_PATH
3. 编辑 config/transferFile.gateway.target.json 中的 brokerHost（开发机/Broker IP）
4. 在目标机创建待传文件，例如: echo test > /tmp/platform_test_file.bin
5. 运行: ./bin/transferFile -c ./config/transferFile.gateway.target.json
