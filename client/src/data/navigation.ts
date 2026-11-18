import generatedNav from '../generated/navigation.json'
import type { TreeItem } from '@nuxt/ui'

export interface DocTreeItem extends TreeItem {
  path?: string | null
  type?: 'directory' | 'file'
  hasReadme?: boolean
  hasHeaders?: boolean
  children?: DocTreeItem[]
}

function processTree(items: DocTreeItem[]): DocTreeItem[] {
  return items.map(item => {
    const isFile = item.type === 'file'
    const hasChildren = item.children && item.children.length > 0

    // A folder without children (only its README page, such as a tool folder) gets no folder icon from the tree
    const pageOnlyFolder: boolean = !isFile && !hasChildren
    let icon: string | undefined = undefined
    if (isFile) {
      icon = 'i-lucide-file-code'
    } else if (pageOnlyFolder) {
      icon = 'i-lucide-file-text'
    }

    return {
      ...item,
      icon,
      children: hasChildren ? processTree(item.children!) : item.children
    }
  })
}

const overviewItem: DocTreeItem = {
  label: 'Overview',
  path: '__home',
  type: 'file',
  icon: 'i-lucide-house',
  hasReadme: true,
}

export const navigation: DocTreeItem[] = [overviewItem, ...processTree(generatedNav as DocTreeItem[])]

export function flattenNavigation(items: DocTreeItem[]): DocTreeItem[] {
  const result: DocTreeItem[] = []
  for (const item of items) {
    if (item.path && (item.hasReadme || item.hasHeaders)) {
      result.push(item)
    }
    if (item.children) {
      result.push(...flattenNavigation(item.children))
    }
  }
  return result
}

