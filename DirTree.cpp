#include <windows.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <shlwapi.h>

// File-system path iterator
class CFSPathIterator
{
public:
    std::wstring m_strFullName;
    size_t m_ich;

    CFSPathIterator(std::wstring strFullName) : m_strFullName(strFullName), m_ich(0)
    {
    }

    std::wstring GetPath() const
    {
        if (!m_ich || m_ich >= m_strFullName.size())
            return m_strFullName;
        return m_strFullName.substr(0, m_ich - 1);
    }

    bool GetNext(std::wstring& strNext)
    {
        if (m_ich >= m_strFullName.size())
            return false;

        auto ich = m_strFullName.find(L'\\', m_ich);
        if (ich == m_strFullName.npos)
        {
            ich = m_strFullName.size();
            strNext = m_strFullName.substr(m_ich, ich - m_ich);
            m_ich = ich;
        }
        else
        {
            strNext = m_strFullName.substr(m_ich, ich - m_ich);
            m_ich = ich + 1;
        }
        return true;
    }
};

class CFSNode;

CFSNode *g_pRoot = NULL;

// File-system node
class CFSNode
{
public:
    std::wstring m_strName;
    BOOL m_bExpand;
    CFSNode* m_pParent;
    std::vector<CFSNode*> m_children;

    CFSNode(const std::wstring& strName, CFSNode* pParent = NULL)
        : m_strName(strName)
        , m_bExpand(FALSE)
        , m_pParent(pParent)
    {
        if (!g_pRoot)
            g_pRoot = this;
    }

    ~CFSNode()
    {
        clear();
    }

    std::wstring GetFullName()
    {
        std::wstring ret;
        if (m_pParent)
            ret = m_pParent->GetFullName();
        if (ret.size())
            ret += L'\\';
        ret += m_strName;
        return ret;
    }

    CFSNode* FindChild(const std::wstring& strName)
    {
        for (auto pChild : m_children)
        {
            if (pChild && pChild->m_strName == strName)
                return pChild;
        }
        return NULL;
    }

    BOOL RemoveChild(CFSNode *pNode)
    {
        for (auto& pChild : m_children)
        {
            if (pChild == pNode)
            {
                auto pOld = pChild;
                pChild = NULL;
                delete pOld;
                return TRUE;
            }
        }
        return FALSE;
    }

    BOOL Remove()
    {
        if (m_pParent)
            return m_pParent->RemoveChild(this);
        return FALSE;
    }

    CFSNode* Find(const std::wstring& strFullName)
    {
        CFSPathIterator it(strFullName);
        std::wstring strName;
        CFSNode *pChild, *pNode;
        for (pNode = this; it.GetNext(strName); pNode = pChild)
        {
            pChild = pNode->FindChild(strName);
            if (!pChild)
                return NULL;
        }
        return pNode;
    }

    void MarkNotExpanded()
    {
        for (auto pNode = this; pNode; pNode = pNode->m_pParent)
            pNode->m_bExpand = FALSE;
    }

    CFSNode* BuildPath(const std::wstring& strFullName, BOOL bMarkNotExpanded = TRUE)
    {
        CFSPathIterator it(strFullName);
        std::wstring strName;
        CFSNode *pNode, *pChild = NULL;
        for (pNode = this; it.GetNext(strName); pNode = pChild)
        {
            pChild = pNode->FindChild(strName);
            if (pChild)
                continue;

            pChild = new CFSNode(strName, pNode);
            pNode->m_children.push_back(pChild);
            if (bMarkNotExpanded)
                pNode->MarkNotExpanded();
        }
        return pNode;
    }

    void Expand()
    {
        if (m_bExpand)
            return;

        auto strSpec = GetFullName();
        strSpec += L"\\*";

        WIN32_FIND_DATAW find;
        HANDLE hFind = ::FindFirstFileW(strSpec.c_str(), &find);
        if (hFind == INVALID_HANDLE_VALUE)
            return;

        do
        {
            if (lstrcmpW(find.cFileName, L".") == 0 ||
                lstrcmpW(find.cFileName, L"..") == 0 ||
                !(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                (find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            {
                continue;
            }

            auto pNode = FindChild(find.cFileName);
            if (!pNode)
            {
                pNode = new CFSNode(find.cFileName, this);
                m_children.push_back(pNode);
            }
            pNode->Expand();
        } while (::FindNextFileW(hFind, &find));
        ::FindClose(hFind);

        m_bExpand = TRUE;
    }

    void Print(int level = 0)
    {
        if (level)
        {
            printf("%s", std::string(level, ' ').c_str());
            if (m_bExpand)
                printf("<1>");
            else
                printf("<0>");
            printf("<%ls>: %ls ", m_strName.c_str(), GetFullName().c_str());
            if (m_pParent)
                printf("<%ls>", m_pParent->m_strName.c_str());
            printf("\n");
        }
        for (auto pChild : m_children)
        {
            if (pChild)
                pChild->Print(level + 1);
        }
    }

    void clear()
    {
        for (auto& pChild : m_children)
        {
            delete pChild;
            pChild = NULL;
        }
        m_children.clear();
    }
};

class CDirectoryList
{
public:
    CDirectoryList()
    {
    }

    CDirectoryList(LPCWSTR pszDirectoryPath)
    {
        AddPathsFromDirectory(pszDirectoryPath);
    }

    BOOL ContainsPath(LPCWSTR pszPath) const;
    BOOL AddPath(LPCWSTR pszPath);
    BOOL AddPathsFromDirectory(LPCWSTR pszDirectoryPath);
    BOOL RenamePath(LPCWSTR pszPath1, LPCWSTR pszPath2);
    BOOL DeletePath(LPCWSTR pszPath);

    void RemoveAll();
};

BOOL CDirectoryList::ContainsPath(LPCWSTR pszPath) const
{
    return !!g_pRoot->Find(pszPath);
}

BOOL CDirectoryList::AddPath(LPCWSTR pszPath)
{
    return !!g_pRoot->BuildPath(pszPath);
}

BOOL CDirectoryList::AddPathsFromDirectory(LPCWSTR pszDirectoryPath)
{
    CFSNode* pNode = g_pRoot->BuildPath(pszDirectoryPath);
    pNode->Expand();
    return TRUE;
}

BOOL CDirectoryList::RenamePath(LPCWSTR pszPath1, LPCWSTR pszPath2)
{
    CFSNode* pNode = g_pRoot->Find(pszPath1);
    if (pNode)
    {
        LPWSTR pch = wcsrchr(pszPath2, L'\\');
        if (pch)
        {
            pNode->m_strName = pch + 1;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CDirectoryList::DeletePath(LPCWSTR pszPath)
{
    CFSNode* pNode = g_pRoot->Find(pszPath);
    if (pNode)
    {
        pNode->Remove();
        return TRUE;
    }
    return FALSE;
}

void CDirectoryList::RemoveAll()
{
    CFSNode *pNode = g_pRoot;
    g_pRoot = NULL;
    delete pNode;
}

INT WINAPI
WinMain(HINSTANCE   hInstance,
        HINSTANCE   hPrevInstance,
        LPSTR       lpCmdLine,
        INT         nCmdShow)
{
    AllocConsole();

    DWORD dwTick0 = GetTickCount();
    g_pRoot = new CFSNode(L"");
    CDirectoryList dir_list;
    dir_list.AddPathsFromDirectory(L"C:\\Windows\\System32");
    dir_list.AddPathsFromDirectory(L"C:\\Windows\\System32\\zu-ZA");
    dir_list.AddPath(L"C:\\TEST\\TEST");
    dir_list.RenamePath(L"C:\\TEST\\TEST", L"C:\\TEST\\TEST2");
    dir_list.RenamePath(L"C:\\TEST\\TEST2", L"C:\\TEST\\TEST3");
    g_pRoot->Print();
    DWORD dwTick1 = GetTickCount();
    printf("dwTick: %ld\n", dwTick1 - dwTick0);

    return 0;
}
