#include "comObject.h"

comObject::comObject(serialPortParam param)
{
    m_serialPortParam = param;

    m_mysqlDB = new mymysql();
    if (-1 == m_mysqlDB->sql_connect("127.0.0.1", "root", "", "mydata"))
    {
        printLog(LOG_ERROR,"connect mysql db failed!");
    }
    printLog(LOG_INFO, "connect mysql db successed.");
    return;
}

comObject::~comObject()
{
    myCom->close();
}


/*******************************************************************************************
 *  函数名称: init
 * 函数功能：线程执行函数，打开串口
 * 输入参数：
 * 输出参数：
 * 返回值：
 *      0 ： 成功
 *      -1： 失败
 *******************************************************************************************/
void comObject::init()
{
#ifdef MYCOM_EVENTDRIVEN
    //定义串口对象，指定串口名和查询模式，这里使用事件驱动EventDriven
    myCom = new Win_QextSerialPort(m_serialPortParam.strCom, QextSerialBase::EventDriven);
#else
    myCom = new Win_QextSerialPort("COM4", QextSerialBase::Polling);
#endif

    //以读写方式打开串口
    if (false == myCom->open(QIODevice::ReadWrite))
    {
        printLog(LOG_ERROR, "myCom open failed.");
        emit signalString(SIGNAL_COM_OPEN_ERROR);
        return;
    }

    //波特率设置，我们设置为9600
    myCom->setBaudRate((BaudRateType)m_serialPortParam.bundRate);

    //数据位设置，我们设置为8位数据位
    myCom->setDataBits((DataBitsType)m_serialPortParam.dataBits);

    //奇偶校验设置，我们设置为无校验
    myCom->setParity((ParityType)m_serialPortParam.checkOut);

    //停止位设置，我们设置为1位停止位
    myCom->setStopBits((StopBitsType)m_serialPortParam.stopBits);

    //数据流控制设置，我们设置为无数据流控制
    myCom->setFlowControl(FLOW_OFF);


#ifdef MYCOM_EVENTDRIVEN
    //信号和槽函数关联，当串口缓冲区有数据时，进行读串口操作
    connect(myCom, &Win_QextSerialPort::readyRead, this, &comObject::on_slot_readMycom);
#else
    //延时设置，我们设置为延时500ms,这个在Windows下好像不起作用
    myCom->setTimeout(10);

    m_timerCom = new QTimer(this);
    m_timerCom->start(10);

    connect(m_timerCom, &QTimer::timeout, this, &comObject::on_slot_readMycom);
#endif


    if(-1 == sendAtCmd(0))
    {
        printLog(LOG_ERROR, "myCom open failed.");
        emit signalString(SIGNAL_AT_CONNECT_ERROR);
        return;
    }
    sendAtCmd(3);

    m_timerPolling  = new QTimer(this);
    m_timerPolling->start(1000*60);
    connect(m_timerPolling, &QTimer::timeout, this,
            [=]()
    {
        sendAtCmd(8);
    });
    return;
}


/*******************************************************************************************
 *  函数名称: init
 * 函数功能：读取串口数据
 * 输入参数：
 * 输出参数：
 * 返回值：
 *      0 ： 成功
 *      -1： 失败
 *******************************************************************************************/
void comObject::on_slot_readMycom()
{
    QByteArray temp = myCom->readAll();
    m_qStrRecvDataAll.append(temp.data());

    if (m_qStrRecvData.contains("ERROR"))
    {
        emit signalString(SIGNAL_AT_CONNECT_ERROR);
        return;
    }

    int indexOfOneData = m_qStrRecvDataAll.indexOf("OK");
    if (-1 == indexOfOneData)
    {
        return;
    }
    indexOfOneData += 2;
    m_qStrRecvData = m_qStrRecvDataAll.left(indexOfOneData);
    m_qStrRecvDataAll = m_qStrRecvDataAll.right(m_qStrRecvDataAll.size() - indexOfOneData);

    //设置短信格式
    if (m_qStrRecvData.contains("OK") && m_qStrRecvData.contains("CMGF"))
    {
        emit signalString(SIGNAL_AT_CONNECT_OK);
        emit signalString(SIGNAL_UPDATE_DATA);
        m_qStrRecvData.clear();
    }

    //读取一条短信，命令字+索引号
    else if (m_qStrRecvData.contains("OK") && m_qStrRecvData.contains("CMGR"))
    {
        parseDataFromSerialPort();
        m_qStrRecvData.clear();
    }

    //读取所有短信 Cmd:8
    else if(m_qStrRecvData.contains("OK") && m_qStrRecvData.contains("CMGL"))
    {
        parseAllSmsFromSerialPort();
        m_qStrRecvData.clear();
        emit signalString(SIGNAL_UPDATE_DATA);
    }

    //短信容量查询 Cmd:6
    else if(m_qStrRecvData.contains("OK") && m_qStrRecvData.contains("CPMS"))
    {
        printLog(LOG_ERROR, "recv msg:%s", m_qStrRecvData.toLatin1().data());
        parseSmsCountFromSerialPort();
        m_qStrRecvData.clear();
    }

    //删除某一条短信
    else if(m_qStrRecvData.contains("OK") && m_qStrRecvData.contains("CMGD"))
    {
        printLog(LOG_ERROR, "delete msg:%s", m_qStrRecvData.toLatin1().data());
        m_qStrRecvData.clear();
    }

    return;
}

int comObject::sendAtCmd(int index, int smsCount)
{
    QString qStrCmd;
    switch(index)
    {
        case 0:
            qStrCmd = "AT\r";
            break;
        case 1:
            //获取电池信息
            qStrCmd = "AT+CCED\r";
            break;
        case 2:
            //获取信号质量
            qStrCmd = "AT+CSQ\r";
            break;
        case 3:
            //设置短信格式
            qStrCmd= "AT+CMGF=1\r";
            break;
        case 4:
            //读取一条短信，命令字+索引号
            qStrCmd= QString("AT+CMGR=%1\r").arg(smsCount);
            break;
        case 5:
            //设置新短信通知指令
            qStrCmd="AT+CNMI=2,1\r";
            break;
        case 6:
            //短信容量查询指令
            qStrCmd = "AT+CPMS?\r";
            break;
        case 7:
            //读取未读短信
            qStrCmd= "AT+CMGL=0\r";
            break;
        case 8:
            //读取所有短信
            qStrCmd= "AT+CMGL=\"ALL\"\r";
            break;
        case 9:
            //短信删除指令
            qStrCmd= QString("AT+CMGD=%1\r").arg(smsCount);
            break;

        default:
            break;
    }


    if(-1 == myCom->write(qStrCmd.toUtf8().data()))
    {
        printLog(LOG_ERROR, "cmd:%s, send AT failed!", qStrCmd.toUtf8().data());
        return -1;
    }
    printLog(LOG_INFO, "msg:%s, send AT successed.", qStrCmd.toUtf8().data());
    return 0;
}

void comObject::parseDataFromSerialPort()
{
    printLog(LOG_ERROR, "%s.", m_qStrRecvData.toLatin1().data());

    int iStatusNum = m_qStrRecvData.indexOf("REC UNREAD", 12);
    if(-1 != iStatusNum)
    {
        int iPhoneNum1 = m_qStrRecvData.indexOf(",\"", iStatusNum);
        int iPhoneNum2 = m_qStrRecvData.indexOf("\",", iPhoneNum1);
        if (-1 == iPhoneNum1 || -1 == iPhoneNum2)
        {
            printLog(LOG_ERROR, "Parsing the SMS number index error!");
            return;
        }
        m_smsInfo.strPhoneNum = m_qStrRecvData.mid(iPhoneNum1+2, iPhoneNum2-iPhoneNum1-2);

        int iTimeNum1 = m_qStrRecvData.indexOf(",\"", iPhoneNum2);
        int iTimeNum2 = m_qStrRecvData.indexOf("\"\r\n", iTimeNum1);
        if (-1 == iTimeNum1 || -1 == iTimeNum2)
        {
            printLog(LOG_ERROR, "Parsing the SMS time index error!");
            return;
        }
        m_smsInfo.strTime = m_qStrRecvData.mid(iTimeNum1+2, iTimeNum2-iTimeNum1-2);

        int iSmsEndNum = m_qStrRecvData.indexOf("\r\n\r\nOK");
        m_smsInfo.strSms = m_qStrRecvData.mid(iTimeNum2+3, iSmsEndNum-iTimeNum2-3);
    }


//    printLog(LOG_INFO, "Msg-->\rphoneNum:%s\rtime:%s\rsmsData:%s", m_smsInfo.strPhoneNum.toUtf8().data(),
//             m_smsInfo.strTime.toUtf8().data(), m_smsInfo.strSms.toUtf8().data());

    parseSmsData();
    return;
}

void comObject::parseSmsCountFromSerialPort()
{
    int flagIndexOFCount1 = m_qStrRecvData.indexOf("CPMS: ", 8);
    int flagIndexOFCount2 = m_qStrRecvData.indexOf("\r\n\r\nOK", flagIndexOFCount1);

    QStringList valueList = m_qStrRecvData.mid(flagIndexOFCount1 + 6, flagIndexOFCount2 - flagIndexOFCount1-6).split(",");
    cout << valueList;

    for(int index = 0; index < valueList.size(); (index+3))
    {
        if(0 == index && ("\"MT\"" == valueList.at(index) || "\"ME\"" == valueList.at(index) || "\"SM\"" == valueList.at(index)))
        {
            for(int indexSms = 0; indexSms < valueList.at(index+1).toInt(); ++indexSms)
            {

            }
        }
        if (3 == index && valueList.at(index) != valueList.at(0))
        {

        }
        if (6 == index && valueList.at(index) != valueList.at(0) && valueList.at(index) != valueList.at(3))
        {

        }
    }
}

void comObject::parseAllSmsFromSerialPort()
{
    QStringList allSmsList = m_qStrRecvData.split("+CMGL: ");
    foreach (QString strOneSms, allSmsList)
    {
        int iStatusNum = strOneSms.indexOf("REC UNREAD");
        if(-1 != iStatusNum)
        {
            m_smsInfo.clear();

            //获取手机号码前后所处索引值
            int iPhoneNum1 = strOneSms.indexOf(",\"", iStatusNum);
            int iPhoneNum2 = strOneSms.indexOf("\",", iPhoneNum1);
            if (-1 == iPhoneNum1 || -1 == iPhoneNum2)
            {
                printLog(LOG_ERROR, "Parsing the SMS number index error!");
                return;
            }
            m_smsInfo.strPhoneNum = strOneSms.mid(iPhoneNum1+2, iPhoneNum2-iPhoneNum1-2);

            //获取短信接收事件前后索引值
            int iTimeNum1 = strOneSms.indexOf(",\"", iPhoneNum2);
            int iTimeNum2 = strOneSms.indexOf("\"\r\n", iTimeNum1);
            if (-1 == iTimeNum1 || -1 == iTimeNum2)
            {
                printLog(LOG_ERROR, "Parsing the SMS time index error!");
                return;
            }
            m_smsInfo.strTime = strOneSms.mid(iTimeNum1+2, iTimeNum2-iTimeNum1-2);

            //获取短信前后符索引值
            int iSmsStartNum = iTimeNum2 + 3;
            int iSmsEndNum = strOneSms.indexOf("\r\n", iSmsStartNum);

            //短信固定长度 138
            if((strOneSms.size()-iSmsStartNum-2) < (iSmsEndNum - iSmsStartNum))
            {
                printLog(LOG_ERROR, "The length of text messages is not equal to 138!");
                return;
            }
            m_smsInfo.strSms = strOneSms.mid(iSmsStartNum, 138);

            parseSmsData();

        }
    }
}

void comObject::parseSmsData()
{
    QString strYear, strYearNext;

    cout << m_smsInfo.strSms;
    QStringList smsDataList = m_smsInfo.strSms.split(",");
    int dataCount = smsDataList.size();

    //begin 站点
    m_siteData.strSiteNum = smsDataList.at(0);

    //end 电池电量
    m_siteData.iBattery = smsDataList.at(dataCount-1).right(1).data()->toLatin1();
    m_siteData.iBattery = m_siteData.iBattery / 100;


    for(int i = 1; i < dataCount; ++i)
    {
        QString strTmpData = smsDataList.at(i);

        int index = i % 3;

        //信息上报时间
        if (1 == index)
        {
            if (1 == i)
            {
                strYear = strTmpData.left(2);
                strYearNext = QString::number(strYear.toInt() + 1);
                m_siteData.strTime = strTmpData;
            }
            else
            {
                if ('0' == strTmpData.at(0))
                {
                    strTmpData = strTmpData.right(strTmpData.size()-1);
                    strTmpData.insert(0, strYear.toLatin1());
                    m_siteData.strTime = strTmpData;
                }
                else
                {
                    strTmpData = strTmpData.right(strTmpData.size()-1);
                    strTmpData.insert(0, strYearNext.toLatin1());
                    m_siteData.strTime = strTmpData;
                }
            }

        }

        //depth 埋深
        else if (2 == index)
        {
            m_siteData.iDepth = strTmpData.toFloat() / 100;
        }

        //温度
        else if (0 == index)
        {
            if (5 == strTmpData.size())
            {
                strTmpData.chop(1);
            }

            m_siteData.iTemperature =  strTmpData.toFloat() / 100;

            printLog(LOG_DEBUG, "siteNum:%s, time:%s, depth:%f, temperature:%f, battery:%f.", m_siteData.strSiteNum.toLatin1().data(),
                     m_siteData.strTime.toLatin1().data(), m_siteData.iDepth, m_siteData.iTemperature, m_siteData.iBattery);

            insertDataToMysql();
            m_siteData.clear();
        }
    }
    return;
}

void comObject::insertDataToMysql()
{
    QString strSql = QString("insert cdr_data(site_number, report_time, deepness, temperature, battery_level) values('%1', '%2', %3, %4, %5);").arg(m_siteData.strSiteNum).arg(m_siteData.strTime).arg(m_siteData.iDepth).arg(m_siteData.iTemperature).arg(m_siteData.iBattery);

    if (-1 == m_mysqlDB->sql_exec(strSql.toUtf8().data()))
    {
        printLog(LOG_ERROR, "inset data failed! strSql:%s", strSql.toUtf8().data());
    }
    return;
}
