#include<mysql/mysql.h>
#include<iostream>

#include<cstdlib>
int main()
{
    //初始化连接对象
    MYSQL*conn=mysql_init(nullptr);
    if(conn==nullptr)
    {
        std::cerr<<"mysql_init failed\n";
        return 1;
    }

    //获取数据库密码
    const char* password=std::getenv("MYSQL_PASSWORD");
    if(password==nullptr)
    {
    std::cerr<<"MYSQL PASSWORD IS NOT SET\n";
    mysql_close(conn);
    return 1;
    }


    //建立数据库连接
    MYSQL*connected=mysql_real_connect(
        conn,
        "127.0.0.1",//数据库地址
        "pool_user",//用户名
        password,//密码
        "connection_pool_test",//数据库名称
        3306,//端口
        nullptr,
        0
    );

    if(connected==nullptr){
    std::cerr<<"connected failed:"<<mysql_error(conn)<<"\n";
    mysql_close(conn);
    return 1;   
    }
    std::cout<<"mysql connected successfully!\n";


    //设置客户端字符集
    if(mysql_set_character_set(conn,"utf8mb4")!=0)
    {
        std::cerr<<"set character failed:"<<mysql_error(conn)<<"\n";
        mysql_close(conn);
        return 1;
    }

    //执行数据库查询
    const char* sql="SELECT id,username,email,age FROM users";
    if(mysql_query(conn,sql)!=0)
    {
        std::cerr<<"query failed:"<<mysql_error(conn)<<"\n";
        mysql_close(conn);
        return 1;
    }
    std::cout<<"query executed successfully\n";

    //获取查询结果
    MYSQL_RES *result=mysql_store_result(conn);
    if(result==nullptr)
    {
        std::cerr<<"get result failed:"<<mysql_error(conn)<<"\n";
        mysql_close(conn);
        return 1;
    }


    //获取列数
    unsigned int fields=mysql_num_fields(result);
    MYSQL_ROW row;
    while((row=mysql_fetch_row(result))!=nullptr)
    {
        for(unsigned int i=0;i<fields;i++)
        {
            if(i>0)
            {
                std::cout<<"|";
            }

            if(row[i]!=nullptr)
            {
                std::cout<<row[i];
            }else
            {
                std::cout<<"null";
            }
        }
        std::cout<<"\n";
    }


    //释放查询结果
    mysql_free_result(result);

    //关闭数据库连接
    mysql_close(conn);

    std::cout<<"mysql connection closed\n";
    
    return 0;
}